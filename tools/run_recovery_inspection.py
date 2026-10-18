"""Native Qt recovery/inspection tests against current production Debug objects."""
from pathlib import Path
import argparse, hashlib, json, subprocess
from run_software_demo import environment
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--objects',type=Path,required=True)
    p.add_argument('--build-summary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--scale',choices=['native','1','1.5'],default='native')
    p.add_argument('--port',help='Explicit physical port; hardware mode never selects a different device')
    p.add_argument('--seconds',type=int,default=30)
    a=p.parse_args();r=a.output.resolve();r.mkdir(parents=True,exist_ok=False)
    summary=json.loads(a.build_summary.read_text(encoding='utf-8'))
    assert summary['pass']
    for rel,sha in summary['source_hashes'].items():
        assert hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()==sha,('Stale production object inputs',rel)
    qt,gcc,env=environment()
    for key in ['QT_SCALE_FACTOR','QT_SCREEN_SCALE_FACTORS','QT_FONT_DPI','QT_QPA_PLATFORM','QT_AUTO_SCREEN_SCALE_FACTOR']:
        env.pop(key,None)
    objects=[x for x in a.objects.resolve().glob('*.o') if x.name!='main.o']
    assert any(x.name=='app_window_inspection.o' for x in objects)
    pro=r/'probe.pro'
    pro.write_text('QT += core gui widgets serialport printsupport testlib\nCONFIG += console c++11 debug\nCONFIG -= release app_bundle\nTARGET=recovery_inspection\nINCLUDEPATH += '+
        ' '.join('"'+(ROOT/x).as_posix()+'"' for x in ['UIDesktop','UIDesktop/config','UIDesktop/plot'])+
        '\nSOURCES += "'+(ROOT/('tools/tests/recovery_hardware_probe.cpp' if a.port else 'tools/tests/recovery_inspection_probe.cpp')).as_posix()+'"\nLIBS += '+
        ' '.join('"'+x.as_posix()+'"' for x in objects)+'\n',encoding='utf-8')
    for name,cmd in [('qmake',[qt/'bin/qmake.exe',pro]),('build',[gcc/'mingw32-make.exe','-j2','debug'])]:
        done=subprocess.run([str(x) for x in cmd],cwd=r,env=env,capture_output=True,timeout=180)
        (r/(name+'.log')).write_bytes(done.stdout+done.stderr)
        assert done.returncode==0 and b'warning:' not in (done.stdout+done.stderr).lower(),name
    probe=r/'debug/recovery_inspection.exe'
    assert not a.port or (a.scale=='native' and 15<=a.seconds<=600)
    native=json.loads(subprocess.check_output([probe,'--describe-scale'],cwd=r,env=env))['native_qt_dpr']
    if a.scale!='native':env['QT_SCALE_FACTOR']=str(float(a.scale)/native)
    done=subprocess.run([probe]+([a.port,str(a.seconds)] if a.port else []),cwd=r,env=env,capture_output=True,timeout=max(180,a.seconds+180))
    (r/'run.log').write_bytes(done.stdout+done.stderr)
    result=json.loads(done.stdout)
    result['input_source_hashes']=summary['source_hashes']
    result['requested_scale']=a.scale;result['platform']='Native Windows qwindows'
    result['unscaled_native_qt_dpr']=native
    if a.scale!='native':
        assert abs(result['device_pixel_ratio']-float(a.scale))<.01, 'Actual Qt scale did not match request'
    (r/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:result[k] for k in ['pass','checks','failures','device_pixel_ratio','requested_scale']},indent=2))
    return done.returncode
if __name__=='__main__':raise SystemExit(main())
