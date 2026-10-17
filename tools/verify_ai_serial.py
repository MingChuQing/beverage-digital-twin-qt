"""Replay a genuinely validated report through two native Qt GUIs and physical TTL ports.

The UI review is simulated and recorded as TEST_ONLY, never as human semantic evidence.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid
import hashlib

ROOT=Path(__file__).resolve().parents[1]
sys.stdout.reconfigure(encoding='utf-8')
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--report',type=Path,required=True)
parser.add_argument('--receiver-port',default='COM10')
parser.add_argument('--sender-port',default='COM3')
args=parser.parse_args()
if args.receiver_port.lower()==args.sender_port.lower():parser.error('Ports must differ')
folder=ROOT/'build'/('ai-ui-test-'+uuid.uuid4().hex[:6])
shutil.copytree(args.report.resolve().parent,folder)
report=json.loads((folder/'report.json').read_text(encoding='utf8'))
report['candidate']=str(folder/'candidate')
(folder/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
environment=os.environ.copy();environment['QT_FORCE_STDERR_LOGGING']='1'
windows=environment.get('SystemRoot',r'C:\Windows')
environment['PATH']=os.pathsep.join([os.path.join(windows,'System32'),windows])
for name in ('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH'):
    environment.pop(name,None)
exe=ROOT/'dist/BeverageTwinDemo.exe'
for relative in ('ai/pipeline.py','ai/provenance.json'):
    if hashlib.sha256((ROOT/relative).read_bytes()).digest()!=hashlib.sha256((ROOT/'dist'/relative).read_bytes()).digest():
        raise RuntimeError('Deployment is stale: '+relative)
results=[]
def record(name,process,stdout,stderr):
    try:output=(stdout+stderr).decode('utf8')
    except UnicodeDecodeError:output=(stdout+stderr).decode('mbcs' if os.name=='nt' else 'utf8','replace')
    value=dict(name=name,passed=process.returncode==0 and 'PASS:' in output,exit_code=process.returncode,output=output)
    results.append(value);print(name+': '+str(value['passed'])+'\n'+output,flush=True)
activation=str(folder/'activation.json')
receiver=subprocess.Popen([str(exe),'--port',args.receiver_port,'--receiver-serial-test','--ai-active-file',activation,'--ai-allow-test-profile','--capture',str(ROOT/'ai-receiver.png')],env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
try:
    time.sleep(1.5)
    sender=subprocess.Popen([str(exe),'--sender','--port',args.sender_port,'--ai-active-file',activation,'--ai-report',str(folder/'report.json'),
                             '--ai-playback-test','--capture',str(ROOT/'ai-sender.png')],env=environment,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    try: out,err=sender.communicate(timeout=115)
    except subprocess.TimeoutExpired:
        sender.kill();out,err=sender.communicate();err+=b'\nFAIL: sender test timeout'
    record('AI Qt sender / actual validated JSON',sender,out,err)
    if not results[-1]['passed']:receiver.terminate()
    try:out,err=receiver.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        receiver.kill();out,err=receiver.communicate();err+=b'\nFAIL: receiver test timeout'
    record('3D Qt receiver / physical serial',receiver,out,err)
    review=json.loads((folder/'human_review.json').read_text(encoding='utf8')) if (folder/'human_review.json').exists() else {}
    results.append(dict(name='review recorded honestly as TEST_ONLY',passed=review.get('status')=='TEST_ONLY'))
finally:
    if receiver.poll() is None:receiver.kill();receiver.communicate()
(ROOT/'verification-ai-serial.json').write_text(json.dumps(dict(results=results,test_report=str(folder/'report.json')),ensure_ascii=False,indent=2),encoding='utf8')
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
