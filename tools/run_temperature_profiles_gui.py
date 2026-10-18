"""Test actual production Qt objects: evidence pairing, curve controls and live CAL."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
from run_software_demo import environment
from desktop_version import desktop_version
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('--objects',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--port');p.add_argument('--stability-seconds',type=int,default=310)
    p.add_argument('--build-summary',type=Path);p.add_argument('--expected-firmware');a=p.parse_args();r=a.output.resolve();r.mkdir(parents=True,exist_ok=False)
    summary=json.loads(a.build_summary.read_text(encoding='utf-8')) if a.build_summary else None
    if summary:
        assert summary['pass']
        for rel,h in summary['source_hashes'].items():assert hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()==h,rel
    if a.port:assert a.expected_firmware,'Explicit --expected-firmware required for hardware'
    assert 30<=a.stability_seconds<=600
    qt,gcc,env=environment()
    for key in ['QT_SCALE_FACTOR','QT_SCREEN_SCALE_FACTORS','QT_FONT_DPI','QT_QPA_PLATFORM']:
        env.pop(key,None)
    env['TEMPHUMI_SETTINGS_PATH']=str(r/'preferences.ini')
    env['TEMPHUMI_TEST_DESKTOP_VERSION']=desktop_version()
    env['TEMPHUMI_TEST_EXPECTED_FIRMWARE']=a.expected_firmware or ''
    objects=[x for x in a.objects.resolve().glob('*.o') if x.name!='main.o'];assert any(x.name=='mainwindow.o' for x in objects)
    pro=r/'probe.pro';pro.write_text('QT += core gui widgets serialport printsupport testlib\nCONFIG += console c++11 debug\nCONFIG -= release app_bundle\nTARGET=profiles\nINCLUDEPATH += '+ ' '.join('"'+(ROOT/x).as_posix()+'"' for x in ['UIDesktop','UIDesktop/config','UIDesktop/plot'])+'\nSOURCES += "'+(ROOT/'tools/tests/temperature_profiles_qt_probe.cpp').as_posix()+'"\nLIBS += '+' '.join('"'+x.as_posix()+'"' for x in objects)+'\n',encoding='utf-8')
    for name,cmd in [('qmake',[qt/'bin/qmake.exe',pro]),('build',[gcc/'mingw32-make.exe','-j2','debug'])]:
        s=subprocess.run([str(x) for x in cmd],cwd=r,env=env,capture_output=True);(r/(name+'.log')).write_bytes(s.stdout+s.stderr)
        assert s.returncode==0,(name,(s.stdout+s.stderr).decode(errors='replace')[-3000:])
    proc=subprocess.run([str(r/'debug/profiles.exe')]+([a.port,str(a.stability_seconds)] if a.port else []),cwd=r,env=env,capture_output=True,timeout=750)
    (r/'run.log').write_bytes(proc.stdout+proc.stderr)
    result=json.loads(proc.stdout)
    if summary:result['production_input_hashes']=summary['source_hashes']
    (r/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:result[k] for k in ['pass','checks','failures','device_pixel_ratio']},indent=2));return proc.returncode
if __name__=='__main__':raise SystemExit(main())
