"""Compile the complete vendored ELM implementation with a scripted adapter."""
from pathlib import Path
import subprocess
import ziglang
root=Path(__file__).resolve().parents[2]
out=root.parent/'outputs/query-regression';out.mkdir(parents=True,exist_ok=True)
zig=Path(ziglang.__file__).parent/'zig.exe'
exe=out/'query-regression.exe'
subprocess.run([str(zig),'c++','-std=c++17','-target','x86_64-windows-gnu','-O0','-g',
    '-I',str(root/'test/host/arduino_elm'),'-I',str(root/'vendor/ELMDuino/src'),'-I',str(root/'lib/BLETrace/src'),
    str(root/'vendor/ELMDuino/src/ELMduino.cpp'),str(root/'test/host/elm_query_regression.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
json_exe=out/'query-json-regression.exe'
subprocess.run([str(zig),'c++','-std=c++17','-target','x86_64-windows-gnu','-O0',
    '-I',str(root/'src'),'-I',str(root/'vendor/ELMDuino/src'),
    '-I',str(root/'.pio/libdeps/WS-SIM7670G-V2_BLE/ArduinoJson/src'),
    str(root/'test/host/query_config_json.cpp'),'-o',str(json_exe)],check=True)
subprocess.run([str(json_exe)],check=True)
