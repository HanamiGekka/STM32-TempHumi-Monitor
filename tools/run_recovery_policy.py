"""Build pure serial identity/retry vectors and read-only native adapter catalog."""
from pathlib import Path
import argparse,json,subprocess
from run_software_demo import environment
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);qt,gcc,env=environment()
    pro=out/'policy.pro';pro.write_text('QT += core serialport\nCONFIG += console c++11\nCONFIG -= app_bundle\nTARGET=policy\nINCLUDEPATH += "'+(ROOT/'UIDesktop').as_posix()+'"\nSOURCES += '+
      ' '.join('"'+(ROOT/x).as_posix()+'"' for x in ['tools/tests/serial_recovery_policy_probe.cpp','UIDesktop/app_serial_recovery.cpp','UIDesktop/app_desktop_settings.cpp'])+'\n',encoding='utf-8')
    for name,command in [('qmake',[qt/'bin/qmake.exe',pro]),('build',[gcc/'mingw32-make.exe','-j2'])]:
        r=subprocess.run([str(x) for x in command],cwd=out,env=env,capture_output=True,timeout=90);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        assert r.returncode==0 and b'warning:' not in (r.stdout+r.stderr).lower(),name
    r=subprocess.run([out/'release/policy.exe'],cwd=out,env=env,capture_output=True,timeout=20)
    (out/'run.log').write_bytes(r.stdout+r.stderr);result=json.loads(r.stdout)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2));return r.returncode
if __name__=='__main__':raise SystemExit(main())
