"""Native persistent-Inspector/local-time/Help/Exit regression on frozen objects."""
from pathlib import Path
import argparse,hashlib,json,subprocess
from run_software_demo import environment
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--objects',type=Path,required=True);p.add_argument('--build-summary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--scale',choices=['1','1.5'],required=True);a=p.parse_args()
    summary=json.loads(a.build_summary.read_text(encoding='utf-8'))
    assert summary['pass']
    for rel,h in summary['source_hashes'].items():assert hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()==h,rel
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);qt,gcc,env=environment()
    for key in ['QT_SCALE_FACTOR','QT_SCREEN_SCALE_FACTORS','QT_FONT_DPI','QT_QPA_PLATFORM','QT_AUTO_SCREEN_SCALE_FACTOR']:env.pop(key,None)
    objects=[x for x in a.objects.resolve().glob('*.o') if x.name!='main.o']
    pro=out/'probe.pro';pro.write_text('QT += core gui widgets serialport printsupport testlib\nCONFIG += console debug c++11\nCONFIG -= release app_bundle\nTARGET=desktop152\nINCLUDEPATH += '+
      ' '.join('"'+(ROOT/x).as_posix()+'"' for x in ['UIDesktop','UIDesktop/config','UIDesktop/plot'])+
      '\nSOURCES += "'+(ROOT/'tools/tests/desktop152_probe.cpp').as_posix()+'"\nLIBS += '+' '.join('"'+x.as_posix()+'"' for x in objects)+'\n',encoding='utf-8')
    for name,command in [('qmake',[qt/'bin/qmake.exe',pro]),('build',[gcc/'mingw32-make.exe','-j2','debug'])]:
        r=subprocess.run([str(x) for x in command],cwd=out,env=env,capture_output=True,timeout=180);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        assert r.returncode==0 and b'warning:' not in (r.stdout+r.stderr).lower(),name
    exe=out/'debug/desktop152.exe';native=json.loads(subprocess.check_output([exe,'--describe-scale'],cwd=out,env=env))['native_qt_dpr']
    env['QT_SCALE_FACTOR']=str(float(a.scale)/native)
    r=subprocess.run([exe,summary['version']],cwd=out,env=env,capture_output=True,timeout=240);(out/'run.log').write_bytes(r.stdout+r.stderr)
    result=json.loads(r.stdout);result['production_input_hashes']=summary['source_hashes'];result['requested_scale']=a.scale
    assert abs(result['device_pixel_ratio']-float(a.scale))<.01
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:result[k] for k in ['pass','checks','failures','device_pixel_ratio','requested_scale']},indent=2));return r.returncode
if __name__=='__main__':raise SystemExit(main())
