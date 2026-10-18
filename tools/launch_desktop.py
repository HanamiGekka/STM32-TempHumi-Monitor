"""Controlled development launch: check/build Release, then open the real GUI."""
from pathlib import Path
import argparse,subprocess,sys
from datetime import datetime,timezone
ROOT=Path(__file__).resolve().parents[1]
from run_software_demo import environment
from desktop_version import desktop_version_path
def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--rebuild',action='store_true'); options,arguments=parser.parse_known_args()
    exe=ROOT/'build_verify/desktop_finalization_20261012/qt_release/release/UIDesktop.exe'
    sources=[ROOT/'VERSION',desktop_version_path()]+[x for x in (ROOT/'UIDesktop').rglob('*') if x.suffix in ['.cpp','.h','.ui','.pro','.rc','.qrc','.png','.ico','.html']]
    if options.rebuild or not exe.is_file() or any(x.stat().st_mtime>exe.stat().st_mtime for x in sources):
        label='launch_'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        subprocess.run([sys.executable,str(ROOT/'tools/build_desktop.py'),'--configuration','release','--label',label],cwd=ROOT,check=True)
    _,_,env=environment()
    process=subprocess.Popen([str(exe)]+arguments,cwd=exe.parent,env=env)
    print('Started TempHumi PID '+str(process.pid)+' from '+str(exe))
if __name__=='__main__': main()
