"""Explicit mock-HTTP repair regression plus activation/tamper checks. No live API calls."""
import contextlib
import copy
import io
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import shutil
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'build/bin/ai'))
import pipeline as p

class PipelineTests(unittest.TestCase):
    def test_probe_timeout_is_a_tool_error(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);candidate=output/'candidate';shutil.copytree(p.BASELINE,candidate)
            with patch.object(p.subprocess,'run',side_effect=p.subprocess.TimeoutExpired('mock-probe',40)):
                with self.assertRaises(p.InfrastructureError):p.cpp_check(candidate,output)
    def test_original_cip_drain_wire_mapping(self):
        p.check_wire_trace([dict(kind='modbus',device='CIPValve',request='05 00 14 FF 00')])
        with self.assertRaises(p.InfrastructureError):
            p.check_wire_trace([dict(kind='modbus',device='CIPValve',request='05 00 15 FF 00')])
    def test_strict_json(self):
        for text in ['{"x":1,"x":2}','{"x":NaN}','[] trailing']:
            with self.subTest(text=text),self.assertRaises(ValueError):p.strict_json(text)

    def test_paths_and_frozen_files(self):
        with tempfile.TemporaryDirectory() as directory:
            candidate=Path(directory)/'candidate';shutil.copytree(p.BASELINE,candidate)
            for name in ['../escape.json','device_registry.json','layered/L1_action/all_actions.json','layered/L2_node/all_nodes.json']:
                change=dict(changes=[dict(file=name,op='create',path='',value={})])
                with self.subTest(name=name),self.assertRaises(ValueError):p.checks.apply_changes(candidate,change)
            self.assertFalse((candidate.parent/'escape.json').exists())

    def test_mock_http_repair_full_validation_and_stale_activation(self):
        fixture=(ROOT/'tests/fixtures/extension_changes.json').read_text(encoding='utf-8-sig')
        requests=[]
        class Handler(BaseHTTPRequestHandler):
            def log_message(self,*args):pass
            def do_POST(self):
                body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                self.server.authorization=self.headers.get('Authorization')
                requests.append(body)
                # This first failure is deliberately injected MOCK data, never labeled DeepSeek output.
                raw='{"changes":[{"file":"../escape.json","op":"create","path":"","value":{}}]}' if len(requests)==1 else fixture
                value=dict(model='MOCK-ONLY',choices=[dict(finish_reason='stop',message=dict(content=raw))])
                content=json.dumps(value).encode();self.send_response(200);self.send_header('Content-Length',str(len(content)));self.end_headers();self.wfile.write(content)
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            with tempfile.TemporaryDirectory(prefix='beverage-ai-tests-') as directory:
                session=Path(directory)
                def mock_api(messages,key,model):return p.call_deepseek(messages,key,model,endpoint='http://127.0.0.1:%d/'%server.server_port)
                output=io.StringIO()
                with contextlib.redirect_stdout(output):
                    p.generate(dict(prompt='Explicit MOCK repair test',key='mock-not-a-real-key',model='MOCK-ONLY',max_rounds=2),session,api=mock_api)
                self.assertEqual(len(requests),2)
                self.assertEqual(server.authorization,'Bearer mock-not-a-real-key')
                self.assertIn('whitelist',requests[1]['messages'][-1]['content'])
                self.assertEqual(requests[0]['response_format'],{'type':'json_object'})
                self.assertNotIn('legacy',requests[0]['messages'][1]['content'].split('冻结输入 context JSON:')[0])
                events=[json.loads(row) for row in output.getvalue().splitlines()]
                self.assertTrue(any(e['kind']=='repair' for e in events))
                ready=next(e for e in events if e['kind']=='ready');report_path=ready['report']
                report,candidate=p.checked_report(report_path)
                self.assertEqual((report['normal_runs'],report['fault_runs']),(9,11))
                self.assertTrue(report['baseline_six_traces_unchanged'])
                with self.assertRaises(FileNotFoundError):p.replay(dict(report=report_path,flavor=2,spec=2))
                with contextlib.redirect_stdout(output):
                    p.approve(dict(report=report_path,test_only=True))
                    p.replay(dict(report=report_path,flavor=2,spec=2,excerpt=True,test_only=True))
                review=p.checks.read(Path(report_path).parent/'human_review.json')
                self.assertEqual(review['status'],'TEST_ONLY')
                with self.assertRaises(ValueError):p.replay(dict(report=report_path,flavor=2,spec=2,excerpt=True))
                replay=p.checks.read(Path(report_path).parent/'last_replay.json')
                stages=[j['value'] for j in replay['jobs'] if j['kind']=='stage']
                self.assertEqual(stages,list(range(12)))
                fill=[j for j in replay['jobs'] if j['kind']=='modbus' and j['device']=='GrapeFill']
                self.assertTrue(any(bytes.fromhex(j['pdu'])==bytes.fromhex('06 00 10 00 FA') for j in fill))
                self.assertNotIn('mock-not-a-real-key',''.join(path.read_text(encoding='utf8') for path in session.rglob('request.json')))
                trace=Path(report_path).parent/'traces/grape_small.json'
                trace.write_text('[]',encoding='utf8')
                with self.assertRaises(ValueError):p.checked_report(report_path)
                variables=candidate/'system_variables.json';variables.write_text('{}',encoding='utf8')
                with self.assertRaises(ValueError):p.checked_report(report_path)
        finally:
            server.shutdown();server.server_close();thread.join()

    def test_infrastructure_is_not_an_ai_defect(self):
        with tempfile.TemporaryDirectory() as directory:
            old=p.validate
            def broken(*args):raise p.InfrastructureError('MOCK unavailable compiler')
            calls=[]
            def api(*args):calls.append(1);return '{}',{'model':'MOCK-ONLY'}
            p.validate=broken
            try:
                with contextlib.redirect_stdout(io.StringIO()),self.assertRaises(p.InfrastructureError):
                    p.generate(dict(prompt='mock',model='MOCK-ONLY',key='mock',max_rounds=3),Path(directory),api=api)
                self.assertEqual(len(calls),1)
                self.assertEqual(p.checks.read(Path(directory)/'round-01/report.json')['status'],'TOOL_ERROR')
            finally:p.validate=old

if __name__=='__main__':unittest.main(verbosity=2)
