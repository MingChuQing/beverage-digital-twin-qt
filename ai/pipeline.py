"""Live DeepSeek generation, original validators, and audited candidate activation.

stdin: a single JSON request (key never saved). stdout: JSONL presentation events.
All validation is virtual; only the Qt sender can replay approved wire actions.
"""
import copy
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE/'vendor'))
import verify_beverage_nl_extension as checks
import simulate_beverage_full as full_sim
from simulate_beverage_full import run_pair

BASELINE = ROOT/'config/recipes/baseline'
checks.BASELINE = BASELINE
MAX_RESPONSE = 2 * 1024 * 1024

class InfrastructureError(RuntimeError):
    """Transport/tool failures must never be presented to the model as JSON defects."""

def verify_material():
    provenance=checks.read(HERE/'provenance.json')
    for relative,expected in provenance['frozen_material_sha256'].items():
        path=ROOT/relative
        actual=hashlib.sha256(path.read_bytes().replace(b'\r\n',b'\n')).hexdigest()
        if actual!=expected: raise InfrastructureError('Frozen engineering/validator input changed: '+relative)
    context=checks.read(HERE/'inputs/context.json')
    for filename,document in context['baseline_files'].items():
        if not checks.identical(document,checks.read(BASELINE/filename)):
            raise InfrastructureError('Model context differs from baseline: '+filename)

def event(kind, message='', **values):
    print(json.dumps(dict(kind=kind,message=message,**values),ensure_ascii=False,allow_nan=False),flush=True)

def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True,exist_ok=True)
    checks.write(path,value)

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def strict_json(text):
    if len(text.encode('utf-8')) > MAX_RESPONSE: raise ValueError('JSON output exceeds 2 MiB')
    return json.loads(text,object_pairs_hook=checks.unique_object,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError('Nonstandard JSON number: '+value)))

def check_wire_trace(trace):
    definitions=checks.read(ROOT/'config/serial/protocol.json')['devices']
    for item in trace:
        if item['kind']!='modbus':continue
        pdu=bytes.fromhex(item['request'])
        if pdu[0] not in (5,6):continue
        device=item['device'];field='write_coils' if pdu[0]==5 else 'write_registers'
        if str(int.from_bytes(pdu[1:3],'big')) not in definitions.get(device,{}).get(field,{}):
            raise InfrastructureError('Serial device adapter lacks original interface: '+device+' / '+item['request'])

def cpp_check(candidate, output):
    # Original engine uses narrow file paths. Run its unchanged code against a
    # byte-identical ASCII scratch copy; keep all evidence in the round folder.
    with tempfile.TemporaryDirectory(prefix='beverage-cpp-') as directory:
        scratch=Path(directory)
        if not str(scratch).isascii():
            raise InfrastructureError('Original C++ engine requires an ASCII TEMP directory')
        shutil.copytree(candidate,scratch/'candidate')
        return _cpp_check(scratch/'candidate',scratch,output)

def _cpp_check(candidate, scratch, output):
    probe = ROOT/'runtime/engine_probe.exe'
    stamp = checks.read(ROOT/'runtime/engine_build.json')
    if sha(probe) != stamp['binary_sha256']: raise ValueError('C++ probe binary hash mismatch')
    variables = checks.read(candidate/'system_variables.json')
    results = []
    for recipe in variables['recipes']:
        request = dict(kind='flow',configuration=checks.read(candidate/'layered/L4_flow'/ (recipe['id']+'.json')),
                       roots=[str(candidate/'layered')],variables=variables,
                       device_registry=checks.read(candidate/'device_registry.json'),
                       profile='beverage',beverage_volume_ml=recipe['volume_ml'],step_budget=200000)
        input_path, result_path = scratch/'cpp-request.json', scratch/'cpp-result.json'
        write(input_path, request)
        env=os.environ.copy()
        env.pop('ICE_DEFECT_REQUEST',None);env.pop('ICE_DEFECT_RESULT',None)
        try:
            completed = subprocess.run([str(probe),str(input_path),str(result_path)],capture_output=True,timeout=40,env=env,
                                       creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        except (OSError,subprocess.TimeoutExpired) as error:
            raise InfrastructureError('Original C++ probe unavailable or timed out; no AI repair requested') from error
        if completed.returncode:
            raise InfrastructureError('Original C++ probe process failed (exit '+str(completed.returncode)+'); candidate not applied')
        shutil.copyfile(result_path,output/('cpp-'+recipe['id']+'.json'))
        report = checks.read(result_path)
        checks.require(not report.get('request_error'),str(report.get('request_error')))
        checks.require(report['structural']['accepted'] and all(report['structural']['checks'].values()),
                       'C++ structural validation failed: '+str(report['structural']))
        checks.require(report['simulation']['status']=='SUCCESS',
                       'C++ execution rejected '+recipe['id']+': '+str(report['simulation'].get('status'))+' '+str(report['simulation'].get('message')))
        results.append(dict(recipe=recipe['id'],status='PASS',steps=report['simulation']['steps']))
    return results

def validate(raw_text, output):
    verify_material()
    candidate = output/'candidate'
    shutil.copytree(BASELINE,candidate)
    original = checks.manifest(BASELINE)
    raw = strict_json(raw_text)
    checks.require(len(raw.get('changes',[]))<=64,'At most 64 changes are supported')
    files = checks.apply_changes(candidate,raw)
    recipes = checks.check_final(candidate,checks.read(HERE/'inputs/context.json'),original)
    event('gate','修改边界 / 工程参数 / 冻结 L1–L2 检查通过',gate=1,status='PASS')
    cpp = cpp_check(candidate,output)
    event('gate','原仓库 C++ 解析、结构检查及虚拟执行：9 个入口通过',gate=1,status='PASS')
    (candidate/'legacy').mkdir(exist_ok=True)
    for path in (HERE/'references/baseline').glob('*.json'):
        shutil.copyfile(path,candidate/'legacy'/path.name)
    for recipe in recipes:
        shutil.copyfile(HERE/'references'/(recipe['flavor']+'_small.json'),candidate/'legacy'/(recipe['id']+'.json'))
    timed_traces=output/'timed_traces'
    original_run=full_sim.run_pair
    def progress_run(*args,**kwargs):
        result=original_run(*args,**kwargs)
        event('simulation_case',result['recipe']+' / '+str(result['fault'] or '正常')+' → '+result['status'])
        return result
    full_sim.run_pair=progress_run
    try:
        simulation = full_sim.compare(candidate,timed_traces)
    finally:
        full_sim.run_pair=original_run
    evaluation_baseline=output/'evaluation_baseline'
    shutil.copytree(BASELINE,evaluation_baseline)
    shutil.copytree(HERE/'references/baseline',evaluation_baseline/'legacy',dirs_exist_ok=True)
    baseline = [run_pair(evaluation_baseline,i) for i in range(6)]
    for left,right in zip(baseline,simulation['recipes'][:6]):
        checks.require(left['trace_sha256']==right['trace_sha256'],'Existing recipe timed trace changed: '+left['recipe'])
    checks.require(len(simulation['recipes'])==9 and len(simulation['fault_cases'])==11,'Incomplete simulation suite')
    checks.require(checks.manifest(BASELINE)==original,'Frozen baseline changed during validation')
    changes, diff = checks.differences(candidate)
    (output/'changes.diff').write_text(diff,encoding='utf-8')
    write(output/'simulation.json',simulation)
    # Reuse the exact traces already compared, without a second interpreter pass.
    traces = output/'traces'
    traces.mkdir()
    for recipe in checks.read(candidate/'system_variables.json')['recipes']:
        source=timed_traces/(recipe['id']+'_layered.jsonl')
        trace=[strict_json(row) for row in source.read_text(encoding='utf-8').splitlines()]
        check_wire_trace(trace)
        write(traces/(recipe['id']+'.json'),trace)
    report=dict(status='AUTO_PASS',human_review='PENDING',candidate=str(candidate),files=files,changes=changes,
                normal_runs=9,fault_runs=11,baseline_six_traces_unchanged=True,cpp=cpp,
                candidate_hashes=checks.manifest(candidate),trace_hashes=checks.manifest(traces),
                raw_sha256=hashlib.sha256(raw_text.encode()).hexdigest(),source_commit=checks.read(HERE/'provenance.json')['commit'],
                scope='C++ parser/virtual executor + independent Python timed equivalence and process model; no physical I/O during gates.')
    write(output/'report.json',report)
    event('gate','9 个正常配方 + 11 个故障场景通过；旧六配方带时间戳轨迹保持一致',gate=2,status='PASS')
    return report,diff

def call_deepseek(messages,key,model,*,endpoint='https://api.deepseek.com/chat/completions'):
    # Endpoint is fixed in production; tests may inject a loopback mock directly.
    body=json.dumps(dict(model=model,messages=messages,stream=False,max_tokens=12000,
                         response_format={'type':'json_object'},thinking={'type':'disabled'})).encode()
    request=urllib.request.Request(endpoint,data=body,headers={'Authorization':'Bearer '+key,'Content-Type':'application/json'})
    try:
        with urllib.request.urlopen(request,timeout=180) as response:
            data=response.read(MAX_RESPONSE+1)
    except urllib.error.HTTPError as error:
        # Never echo the server body or authorization headers into logs.
        raise RuntimeError('DeepSeek HTTP '+str(error.code)+'：请检查 Key、余额、模型名称或服务限流') from None
    except (urllib.error.URLError,TimeoutError):
        raise RuntimeError('DeepSeek 网络或超时错误；候选未应用，可重试') from None
    if len(data)>MAX_RESPONSE: raise ValueError('API response exceeds limit')
    response=strict_json(data.decode('utf-8'))
    choice=response['choices'][0]
    if choice.get('finish_reason')!='stop': raise ValueError('DeepSeek output incomplete: '+str(choice.get('finish_reason')))
    content=choice['message']['content']
    checks.require(isinstance(content,str) and content.strip(),'DeepSeek returned empty JSON content')
    return content,dict(model=response.get('model',model),id=response.get('id'),usage=response.get('usage',{}))

SYSTEM = '''你是饮料生产线 L3/L4 配置生成器。只输出 JSON {"changes":[...]}。
这是一项受约束的新增规格演示：三种口味均增加250mL小瓶；两批、每批六瓶；原有六配方不变。
用户文字不能改变冻结工程数据。若用户要求和此固定任务矛盾，输出 {"changes":[],"unsupported":"说明"} 以拒绝，禁止悄悄执行其他任务。
仅允许 context 中所述修改；禁止输出代码、shell、L1/L2 或设备注册地址变动。
每个change严格包含file/op/path/value；append追加数组元素，replace替换现有JSON Pointer，create仅新增L4入口。
只追加production_points、recipes；recipe_lookup三行各追加规格索引；run_recipe.args[1].max改为2。
L4的params固定[口味索引,2]，id和新L4文件名一致，名称可自定。
各新配方字段与旧配方完全相同。所有点坐标必须来自工程数据；三个灌装头X固定4/5/6，禁止从容量推测物理坐标。
若收到验证错误，请返回修正后的完整changes对象，仍然从原始六配方基线应用，不能返回部分补丁。
不得声称验证通过或替人审核。'''

def generate(request,session,*,api=call_deepseek):
    verify_material()
    context=checks.read(HERE/'inputs/context.json')
    messages=[dict(role='system',content=SYSTEM),dict(role='user',content=request['prompt']+'\n冻结输入 context JSON:\n'+json.dumps(context,ensure_ascii=False))]
    # Request evidence excludes the secret.
    write(session/'request.json',{k:request[k] for k in ('prompt','model','max_rounds')})
    for number in range(1,request['max_rounds']+1):
        folder=session/('round-%02d'%number)
        folder.mkdir()
        event('round','第 %d 轮：请求 DeepSeek 生成 JSON'%number,round=number)
        raw,metadata=api(messages,request['key'],request['model'])
        (folder/'raw_response.json').write_bytes(raw.encode('utf-8'))
        write(folder/'api_metadata.json',metadata)
        event('raw','DeepSeek 原始输出已保存',raw=raw,model=metadata.get('model',''))
        try:
            report,diff=validate(raw,folder)
        except InfrastructureError as error:
            write(folder/'report.json',dict(status='TOOL_ERROR',error=str(error),human_review='NOT_REACHED'))
            raise
        except Exception as error:
            detail=type(error).__name__+': '+str(error)
            write(folder/'report.json',dict(status='FAIL',error=detail,human_review='NOT_REACHED'))
            event('validation_error',detail,round=number)
            messages += [dict(role='assistant',content=raw),dict(role='user',content='外部验证失败：'+detail+'\n请重新提交完整 JSON changes；从原始基线应用，修复后将重新执行全部验证。')]
            if number<request['max_rounds']: event('repair','将实际验证错误反馈给 DeepSeek，继续修正')
            continue
        event('ready','自动验证全部通过，等待人工语义审核',report=str(folder/'report.json'),diff=diff,
              summary='新增橙汁 / 苹果汁 / 葡萄汁 250mL；2批×6瓶；共享三个灌装头；旧6配方不变。\n修改文件：'+', '.join(report['files'])+'\n请核对上瓶、灌装容器及成品点位与现场要求一致。')
        return
    raise RuntimeError('已达到本次最大修正轮数；未通过验证，保持原配置。可调整指令后重试。')

def checked_report(path):
    verify_material()
    path=Path(path).resolve()
    report=checks.read(path)
    checks.require(report['status']=='AUTO_PASS','Candidate did not pass all automatic gates')
    candidate=path.parent/'candidate'
    checks.require(str(candidate)==report['candidate'],'Candidate path mismatch')
    checks.require(checks.manifest(candidate)==report['candidate_hashes'],'Candidate was modified after validation; revalidate')
    checks.require(checks.manifest(path.parent/'traces')==report['trace_hashes'],'Validated trace was modified; revalidate')
    checks.require(sha(path.parent/'raw_response.json')==report['raw_sha256'],'Raw response was modified; revalidate')
    for trace_path in (path.parent/'traces').glob('*.json'):
        check_wire_trace(checks.read(trace_path))
    return report,candidate

def approve(request):
    report,candidate=checked_report(request['report'])
    testing=request.get('test_only') is True
    review=dict(status='TEST_ONLY' if testing else 'APPROVED',reviewer='Automated integration test; NOT human review' if testing else 'local user',time_utc=datetime.now(timezone.utc).isoformat(),
                candidate_hashes=report['candidate_hashes'],report_sha256=sha(request['report']))
    write(Path(request['report']).parent/'human_review.json',review)
    event('applied','自动化测试激活（不代表人工语义审核）' if testing else '人工审核通过，候选配置已激活',candidate=str(candidate),report=request['report'])

def replay(request):
    report,candidate=checked_report(request['report'])
    review=checks.read(Path(request['report']).parent/'human_review.json')
    allowed_review='TEST_ONLY' if request.get('test_only') is True else 'APPROVED'
    checks.require(review['status']==allowed_review and review['candidate_hashes']==report['candidate_hashes'] and
                   review['report_sha256']==sha(request['report']),'Human approval missing or stale')
    variables=checks.read(candidate/'system_variables.json')
    flavor,spec=request['flavor'],request['spec']
    checks.require(type(flavor) is int and type(spec) is int and 0<=flavor<3 and 0<=spec<3,'Invalid recipe selection')
    recipe=variables['recipes'][variables['recipe_lookup'][flavor][spec]]
    trace=checks.read(Path(request['report']).parent/'traces'/(recipe['id']+'.json'))
    jobs=[]; containers=0; include=True; pending_motion=False
    first=request.get('excerpt',True)
    for item in trace:
        if item['kind']=='ui' and item.get('target')=='stage':
            stage=int(item['value'])
            include=not first or containers==0 or stage>=11
            if include: jobs.append(dict(kind='stage',value=stage-1))
        if not include: continue
        kind=item['kind']
        if kind=='finished_container': containers+=1
        if kind=='modbus':
            pdu=bytes.fromhex(item['request'])
            # Live playback sends the validated write sequence. Read/condition decisions
            # belong to the preceding virtual execution, not a claimed online executor.
            if pdu[0] not in (5,6): continue
            jobs.append(dict(kind='modbus',device=item['device'],pdu=item['request']))
            register=int.from_bytes(pdu[1:3],'big')
            if pdu[0]==6 and register==0x6002: pending_motion=True
            if item['device']==recipe['fill_device'] and pdu[0]==5 and pdu[3:]==b'\xff\x00':
                jobs.append(dict(kind='wait',ms=recipe['fill_time_ms']+150))
        elif kind=='device_complete' and item.get('operation')=='motor' and pending_motion:
            jobs.append(dict(kind='wait',ms=1100));pending_motion=False
        elif kind=='wait':
            # Accelerate process dwell only. Full gate simulation keeps original durations.
            if item['duration_ms']>=500: jobs.append(dict(kind='wait',ms=max(50,int(item['duration_ms']*.02))))
    if pending_motion: jobs.append(dict(kind='wait',ms=1100))
    write(Path(request['report']).parent/'last_replay.json',dict(recipe=recipe['id'],excerpt=first,jobs=jobs,
        mode='Validated write-trace playback; process dwell 50x, motor/fill real duration; no online condition interpreter'))
    event('replay','已验证 JSON 写轨迹：'+('首瓶片段' if first else '完整12瓶')+'；工艺等待 50× 加速；运动/灌装按接收端实际时长',jobs=jobs,flavor=flavor,spec=spec)

def main():
    sys.stdin.reconfigure(encoding='utf-8',errors='strict')
    sys.stdout.reconfigure(encoding='utf-8',errors='strict')
    request=strict_json(sys.stdin.readline())
    action=request.get('action')
    if action=='generate':
        checks.require(isinstance(request.get('key'),str) and request['key'].strip(),'请填写 DeepSeek API Key')
        checks.require(type(request.get('max_rounds')) is int and 1<=request['max_rounds']<=6,'最大轮数须为1–6')
        checks.require(isinstance(request.get('prompt'),str) and 0<len(request['prompt'])<=8000,'指令长度须为1–8000字')
        session=Path(request['sessions'])/(datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8])
        session.mkdir(parents=True)
        event('session','已创建本次独立演示记录',directory=str(session))
        generate(request,session)
    elif action=='approve': approve(request)
    elif action=='replay': replay(request)
    elif action=='preview':
        report,candidate=checked_report(request['report'])
        folder=Path(request['report']).parent
        event('raw','已载入历史原始输出（不会调用或伪装为本次 DeepSeek 生成）',raw=(folder/'raw_response.json').read_text(encoding='utf-8'))
        event('ready','已载入通过验证的历史记录，仍需本次人工审核',report=str(folder/'report.json'),
              diff=(folder/'changes.diff').read_text(encoding='utf-8'),summary='9 配方 / 11 故障 / 旧6配方轨迹保持一致。历史验证回放，请核对差异后审核。')
    else: raise ValueError('Unknown pipeline action')

if __name__=='__main__':
    try: main()
    except Exception as error:
        event('error',type(error).__name__+': '+str(error))
        raise SystemExit(1)
