"""Build/test float32 correction, baseline physics, TH3 and disabled behavior."""
from pathlib import Path
import argparse,csv,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from run_software_demo import environment

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    p.add_argument('--rows',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    qt,gcc,env=environment();results=[]
    for enabled in [1,0]:
        exe=a.output/f'calibration_{enabled}.exe';command=[str(gcc/'gcc.exe'),'-std=c11','-Wall','-Wextra','-Werror',
          f'-DALGO_TEMPERATURE_CORRECTION_ENABLED={enabled}','-I',str(ROOT/'Core/Inc'),
          str(ROOT/'tools/tests/temperature_calibration_probe.c'),str(ROOT/'Core/Src/algo_sensor_convert.c'),
          str(ROOT/'Core/Src/app_protocol.c'),'-lm','-o',str(exe)]
        build=subprocess.run(command,env=env,capture_output=True);(a.output/f'build_{enabled}.log').write_bytes(build.stdout+build.stderr)
        assert build.returncode==0,build.stderr.decode(errors='replace')
        result=json.loads(subprocess.check_output([str(exe)],env=env));assert result['pass'];results.append(result)
    rows=list(csv.DictReader(a.rows.open(encoding='utf-8-sig',newline='')));max_diff=0.;field_checks=[]
    for row in rows:
        output=subprocess.check_output([str(a.output/'calibration_1.exe'),row['dut_deg_c']],env=env,text=True).split()
        delta=abs(float(output[1])-float(row['corrected_deg_c']));max_diff=max(max_diff,delta)
        assert delta<2e-5,(row['excel_row'],delta)
        field_checks.append({'excel_row':int(row['excel_row']),'float32_corrected_deg_c':float(output[1]),
                              'correction_id':int(output[2]),'difference_from_python_deg_c':delta})
    # Public C headers also promise C++ linkage; check the new entry point.
    cpp=a.output/'cpp_linkage.cpp'
    cpp.write_text('#include "algo_sensor_convert.h"\nint main(){uint16_t id=0;float v=algo_temperature_correct_mean(26.0f,true,&id);return v>26.89f&&v<26.91f&&id==1?0:1;}\n',encoding='utf-8')
    commands=[[gcc/'gcc.exe','-std=c11','-Wall','-Wextra','-Werror','-I',ROOT/'Core/Inc','-c',ROOT/'Core/Src/algo_sensor_convert.c','-o',a.output/'cpp_algo.o'],
              [gcc/'g++.exe','-std=c++11','-Wall','-Wextra','-Werror','-I',ROOT/'Core/Inc',cpp,a.output/'cpp_algo.o','-o',a.output/'cpp_linkage.exe'],
              [a.output/'cpp_linkage.exe']]
    for command in commands:
        p=subprocess.run([str(x) for x in command],env=env,capture_output=True)
        assert p.returncode==0,p.stderr.decode(errors='replace')
    summary={'pass':True,'cpp_linkage_pass':True,'compiled_modes':results,'all_workbook_rows_checked':len(rows),
             'maximum_float32_model_difference_deg_c':max_diff,'rows':field_checks,
             'classification':'DETERMINISTIC HOST VECTORS; NOT HARDWARE ACCURACY'}
    (a.output/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in summary.items() if k!='rows'},indent=2))
if __name__=='__main__':main()
