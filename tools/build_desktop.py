"""Controlled qmake build. Never edits Qt Creator's personal configuration."""
from pathlib import Path
import argparse,hashlib,json,subprocess,os,re,sys
from desktop_version import desktop_version,desktop_version_path
from datetime import datetime,timezone
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools')); from run_software_demo import environment
def main():
    p=argparse.ArgumentParser(); p.add_argument('--build-root',type=Path,default=ROOT/'build_verify/desktop_finalization_20261012'); p.add_argument('--evidence',type=Path); p.add_argument('--configuration',choices=['debug','release','both'],default='both'); p.add_argument('--label',default='build'); p.add_argument('--regenerate-icon',action='store_true',help='Explicitly regenerate source artwork (requires Pillow and Windows Arial).'); a=p.parse_args()
    qt,gcc,env=environment(); raw=a.evidence or a.build_root/'logs'; raw.mkdir(parents=True,exist_ok=True)
    from update_desktop_resource import update_version_resource
    update_version_resource()
    # Existing reviewed artwork is build input, not an automatically rewritten output.
    if a.regenerate_icon or not (ROOT/'UIDesktop/resources/temphumi.ico').is_file():
        subprocess.run([sys.executable,str(ROOT/'tools/generate_desktop_icon.py')],cwd=ROOT,check=True,capture_output=True)
    source_files=list(dict.fromkeys([ROOT/'VERSION',desktop_version_path()]+[x for x in (ROOT/'UIDesktop').rglob('*') if x.suffix in ['.cpp','.h','.ui','.pro','.qrc','.rc','.ico','.png','.svg','.in','.html']]))
    out={'started_at':datetime.now(timezone.utc).isoformat(),'version':desktop_version(),'firmware_source_version':(ROOT/'VERSION').read_text().strip(),'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip(),'builds':[],'source_hashes':{str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in source_files}}
    configs=['debug','release'] if a.configuration=='both' else [a.configuration]
    for config in configs:
        folder=a.build_root/('qt_'+config); folder.mkdir(parents=True,exist_ok=True)
        result={'configuration':config,'commands':[],'warnings':[],'errors':[]}
        for step,cmd in [('qmake',[qt/'bin/qmake.exe',ROOT/'UIDesktop/UIDesktop.pro','-spec','win32-g++','CONFIG+='+config,'CONFIG-='+('release' if config=='debug' else 'debug')]),('make',[gcc/'mingw32-make.exe','-f','Makefile.'+config.capitalize(),'-j2'])]:
            log=raw/f'{a.label}_{config}_{step}.log'; assert not log.exists(),log
            proc=subprocess.run([str(x) for x in cmd],cwd=folder,env=env,capture_output=True,timeout=600); log.write_bytes(proc.stdout+proc.stderr)
            text=(proc.stdout+proc.stderr).decode('utf-8',errors='replace'); result['commands'].append({'step':step,'exit':proc.returncode,'log':str(log)})
            result['warnings']+=re.findall(r'^.*warning:.*$',text,re.M|re.I); result['errors']+=re.findall(r'^.*error:.*$',text,re.M|re.I)
            if proc.returncode: break
        exe=folder/config/'UIDesktop.exe'; result['exe']=str(exe); result['exe_exists']=exe.exists()
        if exe.exists(): result['exe_sha256']=hashlib.sha256(exe.read_bytes()).hexdigest()
        result['pass']=result['exe_exists'] and all(x['exit']==0 for x in result['commands']) and len(result['commands'])==2 and not result['warnings'] and not result['errors']; out['builds'].append(result)
    out['pass']=all(x['pass'] for x in out['builds']); (raw/(a.label+'_summary.json')).write_text(json.dumps(out,indent=2),encoding='utf-8'); print(json.dumps({k:v for k,v in out.items() if k!='source_hashes'},indent=2),flush=True); return 0 if out['pass'] else 1
if __name__=='__main__': raise SystemExit(main())
