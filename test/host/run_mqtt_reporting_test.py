"""Run real main.cpp reporting functions with deterministic fake hardware.

Example: python run_mqtt_reporting_test.py --cxx /path/to/zig --out /tmp/mqtt-test
The source override can verify that the original firmware fails this regression.
"""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", required=True, help="Zig executable")
parser.add_argument("--out", type=Path, required=True)
parser.add_argument("--source", type=Path, default=Path(__file__).parents[2] / "src/main.cpp")
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")

def extract(signature):
    start = source.index(signature)
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

args.out.mkdir(parents=True, exist_ok=True)
(args.out / "mqtt_reporting_functions.inc").write_text("\n\n".join(
    extract(name) for name in ("bool sendLocationData()", "unsigned long calcTimestamp(", "void mqttSendData()")
), encoding="utf-8")
exe = args.out / ("mqtt_reporting_test.exe" if os.name == "nt" else "mqtt_reporting_test")
subprocess.run([args.cxx, "c++", "-std=c++11", "-nostdlib++", "-Wno-nullability-completeness",
                "-I", str(args.out), str(Path(__file__).with_name("mqtt_reporting_test.cpp")),
                "-o", str(exe)], check=True)
raise SystemExit(subprocess.run([str(exe)]).returncode)
