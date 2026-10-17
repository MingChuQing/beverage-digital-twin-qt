"""Export an original-engine-compatible case with the shared single-bus addresses."""
import argparse
import json
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--port", default="COM3", help="Controller's local serial port (the twin uses COM10)")
parser.add_argument("--profile", choices=["baseline", "extended"], default="extended")
parser.add_argument("--output", type=Path, default=root / "build/single_bus_case")
parser.add_argument("--overwrite", action="store_true")
args = parser.parse_args()
destination = args.output.resolve()
if not args.port.strip():
    parser.error("--port cannot be empty")
if destination.exists() and any(destination.iterdir()) and not args.overwrite:
    parser.error("Output directory is not empty; choose another directory or pass --overwrite")
source = root / "config/recipes" / args.profile
snapshots = (root / "config/recipes").resolve()
if destination == snapshots or snapshots in destination.parents or destination in snapshots.parents:
    parser.error("Output must be separate from the imported configuration snapshots")
shutil.copytree(source, destination, dirs_exist_ok=True)
registry = json.loads((root / "config/serial/device_registry.json").read_text(encoding="utf-8"))
for device in registry["devices"].values():
    device["port"] = args.port.strip()
registry["configuration_note"] = "Controller-side single bus; device IDs and registers match the digital twin."
(destination / "device_registry.json").write_text(json.dumps(registry, indent=2) + "\n", encoding="utf-8")
print("Exported " + str(destination))
print("Controller port: " + args.port + "; 21 devices; motion addresses 1-7; process addresses 21-34")
