"""Verify the deployed executable with a clean Qt environment; GUI needs a desktop."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def png_size(path):
    """Read the PNG IHDR without depending on Pillow or the host Qt installation."""
    with path.open("rb") as stream:
        header = stream.read(24)
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValueError("Not a PNG image: " + str(path))
    return struct.unpack(">II", header[16:24])


root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--exe", type=Path, default=root / "dist/BeverageTwinDemo.exe")
parser.add_argument("--gui", action="store_true", help="Also load the real QML/3D scene and capture images")
parser.add_argument("--small", action="store_true", help="Test the 1120 x 840 layout")
parser.add_argument("--profile", choices=["baseline", "extended", "both"], default="both")
parser.add_argument("--flavor", type=int, choices=range(3), help="0 orange, 1 apple, 2 grape")
parser.add_argument("--format", type=int, choices=range(3), help="0 family, 1 can, 2 small (extended only)")
options = parser.parse_args()
if options.format == 2 and options.profile != "extended":
    parser.error("--format 2 requires --profile extended; baseline contains only two formats")
environment = os.environ.copy()
environment["QT_FORCE_STDERR_LOGGING"] = "1"
system_root = environment.get("SystemRoot", r"C:\Windows")
environment["PATH"] = os.pathsep.join([os.path.join(system_root, "System32"), system_root])
for variable in ["QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"]:
    environment.pop(variable, None)

suffix = "-small" if options.small else ""
checks = [("backend", ["--self-test"], [])]
if options.gui:
    profiles = ["baseline", "extended"] if options.profile == "both" else [options.profile]
    for profile in profiles:
        flavor = options.flavor if options.flavor is not None else (2 if profile == "extended" else 0)
        packaging = options.format if options.format is not None else (2 if profile == "extended" else 0)
        stem = "demo-screenshot-" + profile + suffix
        capture = root / (stem + ".png")
        arguments = ["--smoke-test", "--flavor", str(flavor), "--format", str(packaging), "--capture", str(capture)]
        if profile == "extended":
            arguments.append("--extended")
        if options.small:
            arguments.append("--small-window")
        checks.append(("gui-" + profile + suffix, arguments, [capture, root / (stem + "-fault.png")]))

results = []
for name, arguments, captures in checks:
    # A previous successful run must not supply stale images for this run.
    before = {str(path): path.stat().st_mtime_ns if path.exists() else None for path in captures}
    try:
        process = subprocess.run(
            [str(options.exe.resolve()), *arguments], cwd=root, env=environment,
            capture_output=True, timeout=45,
        )
        raw = process.stdout + process.stderr
        try:
            message = raw.decode("utf-8")
        except UnicodeDecodeError:
            message = raw.decode("mbcs" if os.name == "nt" else "utf-8", errors="replace")
        result = {
            "name": name, "arguments": arguments, "exit_code": process.returncode,
            "passed": process.returncode == 0 and "PASS:" in message, "output": message,
        }
        images = []
        for path in captures:
            try:
                width, height = png_size(path)
                fresh = before[str(path)] != path.stat().st_mtime_ns
                valid_size = (width, height) == (1120, 840) if options.small else width >= 1120 and height >= 840
                images.append({"path": str(path), "width": width, "height": height, "fresh": fresh})
                result["passed"] = result["passed"] and fresh and valid_size
            except (OSError, ValueError) as error:
                result["passed"] = False
                images.append({"path": str(path), "error": str(error)})
        if images:
            result["captures"] = images
    except (OSError, subprocess.TimeoutExpired) as error:
        result = {"name": name, "arguments": arguments, "passed": False, "error": str(error)}
    results.append(result)
    print(name + ": " + ("PASS" if result["passed"] else "FAIL"))
    print(result.get("output", result.get("error", "")))

report = root / ("verification" + suffix + ".json")
report.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
print("Report: " + str(report))
sys.exit(0 if all(result["passed"] for result in results) else 1)
