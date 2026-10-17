"""Verify RTU protocol and, optionally, the user's two physical USB/TTL ports."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--exe", type=Path, default=root / "dist/BeverageTwinDemo.exe")
parser.add_argument("--hardware", action="store_true", help="Open two real serial ports and send a complete cycle")
parser.add_argument("--gui", action="store_true", help="Use separate receiver/sender GUI processes with --hardware")
parser.add_argument("--receiver-port", default="COM10")
parser.add_argument("--sender-port", default="COM3")
options = parser.parse_args()
if options.receiver_port.lower() == options.sender_port.lower():
    parser.error("Receiver and sender ports must be different")
environment = os.environ.copy()
environment["QT_FORCE_STDERR_LOGGING"] = "1"
windows = environment.get("SystemRoot", r"C:\Windows")
environment["PATH"] = os.pathsep.join([os.path.join(windows, "System32"), windows])
for name in ["QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"]:
    environment.pop(name, None)
results = []

def result(name, process, arguments):
    raw = process.stdout + process.stderr
    try:
        output = raw.decode("utf-8")
    except UnicodeDecodeError:
        output = raw.decode("mbcs" if os.name == "nt" else "utf-8", errors="replace")
    value = {"name": name, "arguments": arguments, "exit_code": process.returncode,
             "passed": process.returncode == 0 and "PASS:" in output, "output": output}
    results.append(value)
    print(name + ": " + ("PASS" if value["passed"] else "FAIL"), flush=True)
    print(output, flush=True)

def run(name, arguments, timeout=105):
    try:
        process = subprocess.run([str(options.exe.resolve()), *arguments], env=environment,
                                 cwd=root, capture_output=True, timeout=timeout)
        result(name, process, arguments)
    except (OSError, subprocess.TimeoutExpired) as error:
        results.append({"name": name, "passed": False, "error": str(error)})
        print(name + ": FAIL " + str(error), flush=True)

run("rtu-backend", ["--rtu-self-test"])
if options.hardware and all(item["passed"] for item in results):
    if options.gui:
        receiver_capture = root / "serial-receiver.png"
        sender_capture = root / "serial-sender.png"
        previous = {path: path.stat().st_mtime_ns if path.exists() else None for path in [receiver_capture, sender_capture]}
        receiver_args = ["--port", options.receiver_port, "--baud", "115200", "--receiver-serial-test",
                         "--capture", str(receiver_capture)]
        receiver = subprocess.Popen([str(options.exe.resolve()), *receiver_args], cwd=root, env=environment,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            time.sleep(1.5)
            run("independent-sender-gui", ["--sender", "--extended", "--flavor", "2", "--format", "2",
                "--port", options.sender_port, "--baud", "115200", "--sender-demo-test", "--capture", str(sender_capture)])
            if not results[-1]["passed"]:
                receiver.terminate()
            stdout, stderr = receiver.communicate(timeout=70)
            result("independent-receiver-gui", subprocess.CompletedProcess(receiver.args, receiver.returncode, stdout, stderr), receiver_args)
            for path in [receiver_capture, sender_capture]:
                fresh = path.exists() and path.stat().st_mtime_ns != previous[path] and path.stat().st_size > 20000
                results.append({"name": "capture-" + path.stem, "passed": fresh, "path": str(path)})
        finally:
            if receiver.poll() is None:
                receiver.terminate()
                receiver.communicate(timeout=10)
    else:
        run("physical-serial", ["--serial-smoke-test", "--receiver-port", options.receiver_port,
                                "--sender-port", options.sender_port])
elif options.gui:
    run("receiver-ui", ["--ui-smoke-test", "--capture", str(root / "serial-receiver.png")])
    run("sender-ui", ["--sender", "--extended", "--flavor", "2", "--format", "2",
                      "--ui-smoke-test", "--capture", str(root / "serial-sender.png")])
(root / "verification-serial.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
sys.exit(0 if all(item["passed"] for item in results) else 1)
