"""Bundle an existing Python stdlib and build the original, virtual-only C++ probe."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def prepare(destination, python_base, compiler):
    if not (python_base/'python312.dll').is_file():
        raise RuntimeError('This Windows bundle requires Python 3.12; pass its python.exe with -PythonExecutable.')
    runtime = destination / 'runtime'
    python = runtime / 'python'
    python.mkdir(parents=True, exist_ok=True)
    for name in ('python.exe', 'python3.dll', 'python312.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'LICENSE.txt'):
        source = python_base / name
        if source.exists(): shutil.copyfile(source, python / name)
    shutil.copytree(python_base / 'DLLs', python / 'DLLs', dirs_exist_ok=True)
    archive = python / 'python312.zip'
    if not archive.exists():
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as stream:
            for source in (python_base / 'Lib').rglob('*'):
                relative = source.relative_to(python_base / 'Lib')
                if source.is_file() and source.suffix != '.pyc' and not set(relative.parts) & {'site-packages','__pycache__','test','tests','idlelib','tkinter','turtledemo','ensurepip'}:
                    stream.write(source, relative.as_posix())
    (python / 'python312._pth').write_text('python312.zip\nDLLs\n.\n', encoding='utf-8')
    engine = ROOT / 'ai/engine'
    sources = sorted((engine / 'src').glob('*.cpp')) + [engine / 'defect_probe.cpp']
    inputs = sources + sorted((engine / 'include').rglob('*.hpp')) + sorted((engine / 'third_party').rglob('*.hpp'))
    digest = hashlib.sha256(b''.join(p.read_bytes() for p in inputs)).hexdigest()
    executable = runtime / 'engine_probe.exe'
    stamp = runtime / 'engine_build.json'
    old = json.loads(stamp.read_text()) if stamp.exists() else {}
    if not executable.exists() or old.get('source_sha256') != digest or old.get('binary_sha256') != hashlib.sha256(executable.read_bytes()).hexdigest():
        tool = compiler or os.environ.get('ICE_CXX') or shutil.which('clang++') or shutil.which('g++')
        if not tool: raise RuntimeError('Set ICE_CXX to an existing C++17 compiler (zig, clang++, g++).')
        command = [tool] + (['c++'] if Path(tool).stem.lower() == 'zig' else [])
        command += ['-std=c++17','-O0','-I',str(engine/'include'),'-I',str(engine/'third_party')]
        command += [str(p) for p in sources] + ['-o',str(executable)]
        env = os.environ.copy()
        env['ZIG_GLOBAL_CACHE_DIR'] = str(ROOT/'build/ai-cache-global')
        env['ZIG_LOCAL_CACHE_DIR'] = str(ROOT/'build/ai-cache-local')
        subprocess.run(command, check=True, env=env, timeout=900)
        stamp.write_text(json.dumps({'source_sha256':digest,'binary_sha256':hashlib.sha256(executable.read_bytes()).hexdigest(),'compiler':Path(tool).name},indent=2),encoding='utf-8')
    shutil.copytree(ROOT/'ai', destination/'ai', dirs_exist_ok=True)
    shutil.copytree(ROOT/'config', destination/'config', dirs_exist_ok=True)
    print('AI runtime ready: '+str(destination), flush=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination',type=Path,default=ROOT/'build/bin')
    parser.add_argument('--python-base',type=Path,default=Path(sys.base_prefix))
    parser.add_argument('--compiler')
    args = parser.parse_args()
    prepare(args.destination,args.python_base,args.compiler)
