"""Build/run actual Qt event probes against reviewed desktop production objects."""
from pathlib import Path
import argparse,json,subprocess,os,ctypes
from run_software_demo import environment
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--objects',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--mode',default='baseline');p.add_argument('--scale',default='native');p.add_argument('--native',action='store_true');p.add_argument('--stream',type=Path);p.add_argument('--port');a=p.parse_args()
 qt,gcc,env=environment();r=a.output.resolve();r.mkdir(parents=True,exist_ok=False)
 objects=[x for x in a.objects.resolve().glob('*.o') if x.name not in {'main.o','app_th2.o'}];assert any(x.name=='mainwindow.o' for x in objects)
 pro=r/'probe.pro';pro.write_text('QT += core gui widgets serialport printsupport testlib\nCONFIG += console c++11 debug\nCONFIG -= release app_bundle\nTARGET=ux25\nINCLUDEPATH += '+ ' '.join('"'+(ROOT/x).as_posix()+'"' for x in ['UIDesktop','UIDesktop/config','UIDesktop/plot'])+'\nSOURCES += "'+(ROOT/'tools/tests/ux25_probe.cpp').as_posix()+'"\nLIBS += '+' '.join('"'+x.as_posix()+'"' for x in objects)+'\n',encoding='utf-8')
 for name,cmd in [('qmake',[qt/'bin/qmake.exe',pro]),('build',[gcc/'mingw32-make.exe','-j2','debug'])]:
  result=subprocess.run([str(x) for x in cmd],cwd=r,env=env,capture_output=True);(r/(name+'.log')).write_bytes(result.stdout+result.stderr)
  if result.returncode:raise RuntimeError(name+' failed: '+str(r))
 # Normalize the native desktop's scale locally, without changing Windows settings.
 if a.native and os.name=='nt':
  ctypes.windll.user32.SetProcessDPIAware(); native_scale=int(ctypes.windll.user32.GetDpiForSystem()/96+.5)
 else:native_scale=1
 if a.scale!='native':env['QT_SCALE_FACTOR']=str(float(a.scale)/native_scale)
 env['QT_QPA_FONTDIR']=str(Path(os.environ['SystemRoot'])/'Fonts')
 if not a.native:env['QT_QPA_PLATFORM']='offscreen'
 command=[str(r/'debug/ux25.exe'),a.mode]+([str(a.stream.resolve())] if a.stream else [a.port] if a.port else [])
 result=subprocess.run(command,cwd=r,env=env,capture_output=True,timeout=750)
 (r/'run.log').write_bytes(result.stdout+result.stderr)
 data=json.loads(result.stdout);(r/'result.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
 print(json.dumps({k:data[k] for k in ['pass','checks','failures','device_pixel_ratio','wall_duration_ms','flashed_status_reply'] if k in data}),flush=True)
 return result.returncode
if __name__=='__main__':raise SystemExit(main())
