"""Call real DeepSeek once through the packaged pipeline; never record the key."""
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.stdout.reconfigure(encoding='utf-8')
output=ROOT/'build/ai-live'
output.mkdir(parents=True,exist_ok=True)
request=dict(action='generate',key=os.environ.get('DEEPSEEK_API_KEY',''),model=os.environ.get('DEEPSEEK_MODEL','deepseek-flash'),
             prompt=(ROOT/'ai/inputs/prompt.txt').read_text(encoding='utf-8-sig'),max_rounds=3,sessions=str(output))
python=ROOT/'build/bin/runtime/python/python.exe'
process=subprocess.Popen([str(python),'-u',str(ROOT/'build/bin/ai/pipeline.py')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
process.stdin.write((json.dumps(request,ensure_ascii=False)+'\n').encode());process.stdin.close()
events=[]
for raw in process.stdout:
    value=json.loads(raw)
    events.append(value)
    print(value['kind']+': '+value['message'],flush=True)
code=process.wait()
(output/'events.json').write_text(json.dumps(events,ensure_ascii=False,indent=2),encoding='utf-8')
if code or not any(e['kind']=='ready' for e in events):
    print('FAIL: live API / full validation',flush=True)
    raise SystemExit(1)
ready=next(e for e in events if e['kind']=='ready')
print('PASS: real DeepSeek generation and original validation pipeline\nReport: '+ready['report'])
