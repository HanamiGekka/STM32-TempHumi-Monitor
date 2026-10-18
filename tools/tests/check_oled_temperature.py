"""Verify application -> corrected snapshot -> actual OLED glyph/I2C bytes."""
from pathlib import Path
import argparse, hashlib, json, subprocess, sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from run_software_demo import environment

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=False)
    _,gcc,env=environment();exe=a.output/'oled_temperature_probe.exe'
    inputs=[ROOT/'tools/tests/oled_temperature_probe.c']+[ROOT/'Core/Src'/n for n in (
        'algo_sensor_convert.c','app_protocol.c','app_measurement_source.c',
        'app_measurement_source_sim.c','app_hybrid_source.c')]
    cmd=[gcc/'gcc.exe','-std=c11','-Wall','-Wextra','-Werror',
         '-include',ROOT/'tools/tests/oled_host_hal.h','-I',ROOT/'Core/Inc',*inputs,'-lm','-o',exe]
    proc=subprocess.run([str(x) for x in cmd],env=env,capture_output=True)
    (a.output/'build.log').write_bytes(proc.stdout+proc.stderr)
    if proc.returncode:raise RuntimeError(proc.stderr.decode(errors='replace'))
    result=subprocess.run([str(exe)],env=env,capture_output=True)
    (a.output/'run.log').write_bytes(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stderr.decode(errors='replace'))
    data=json.loads(result.stdout);assert data['pass']
    objects=[]
    abi_log=b''
    for name in ['app_protocol','algo_sensor_convert']:
        obj=a.output/(name+'.o');objects.append(obj)
        cmd=[gcc/'gcc.exe','-std=c11','-Wall','-Wextra','-Werror','-I',ROOT/'Core/Inc',
             '-c',ROOT/'Core/Src'/(name+'.c'),'-o',obj]
        proc=subprocess.run([str(x) for x in cmd],env=env,capture_output=True)
        abi_log+=proc.stdout+proc.stderr
        if proc.returncode:raise RuntimeError(abi_log.decode(errors='replace'))
    abi=a.output/'oled_protocol_cpp_probe.exe'
    cmd=[gcc/'g++.exe','-std=c++11','-Wall','-Wextra','-Werror','-I',ROOT/'Core/Inc',
         ROOT/'tools/tests/oled_protocol_cpp_probe.cpp',*objects,'-lm','-o',abi]
    proc=subprocess.run([str(x) for x in cmd],env=env,capture_output=True)
    abi_log+=proc.stdout+proc.stderr;(a.output/'cpp_abi_build.log').write_bytes(abi_log)
    if proc.returncode:raise RuntimeError(abi_log.decode(errors='replace'))
    subprocess.run([str(abi)],env=env,check=True)
    data['cpp11_header_and_C_linkage']='PASS'
    data['source_hashes']={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
                           for f in inputs+[ROOT/'Core/Src/app_th_meter.c',ROOT/'Core/Src/drv_oled.c',ROOT/'Core/Inc/app_protocol.h',ROOT/'tools/tests/oled_host_hal.h',ROOT/'tools/tests/oled_protocol_cpp_probe.cpp']}
    (a.output/'summary.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in data.items() if k!='source_hashes'},indent=2))
if __name__=='__main__':main()
