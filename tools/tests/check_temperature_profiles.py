"""Cross-check all 49 rows for each deployed profile against independent SciPy fits."""
from pathlib import Path
import argparse,json,subprocess,sys
import numpy as np
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
from run_software_demo import environment
from explore_temperature_models import predict
from analyze_temperature_calibration import weight

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--sweep',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    _,gcc,env=environment();results=[]
    for enabled in (1,0):
        exe=a.output/f'profiles_{enabled}.exe';cmd=[gcc/'gcc.exe','-std=c11','-Wall','-Wextra','-Werror',f'-DALGO_TEMPERATURE_CORRECTION_ENABLED={enabled}','-I',ROOT/'Core/Inc',ROOT/'tools/tests/temperature_calibration_probe.c',ROOT/'Core/Src/algo_sensor_convert.c',ROOT/'Core/Src/app_protocol.c','-lm','-o',exe]
        proc=subprocess.run([str(x) for x in cmd],env=env,capture_output=True);(a.output/f'build_{enabled}.log').write_bytes(proc.stdout+proc.stderr);assert proc.returncode==0,proc.stderr.decode(errors='replace')
        data=json.loads(subprocess.check_output([str(exe)],env=env));assert data['pass'];results.append(data)
    sweep=json.loads(a.sweep.read_text(encoding='utf-8'));records=sweep['original_rows'];x=np.array([v['dut_deg_c'] for v in records]);checks=[]
    for profile,ident in [(1,None),(2,'guarded_piecewise:2,l1'),(3,'guarded_piecewise:6,huber')]:
        expected=x+.9*weight(x) if profile==1 else predict(next(v['model'] for v in sweep['models'] if v['id']==ident),x)
        for row,value in zip(records,expected):
            fields=subprocess.check_output([str(a.output/'profiles_1.exe'),str(row['dut_deg_c']),str(profile)],env=env,text=True).split();delta=abs(float(fields[1])-value)
            assert delta<2e-5,(profile,row['excel_row'],delta)
            checks.append({'profile':profile,'excel_row':row['excel_row'],'float32':float(fields[1]),'expected':float(value),'difference':float(delta)})
    summary={'pass':True,'compiled_modes':results,'vector_comparisons':checks,'maximum_float32_difference_deg_c':max(v['difference'] for v in checks),'classification':'Deterministic host verification; not independent physical accuracy'}
    (a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:v for k,v in summary.items() if k!='vector_comparisons'},indent=2))
if __name__=='__main__':main()
