"""Read-only verification of retained unified desktop/MCU release bundles."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, zipfile, os, subprocess

def sha(data):return hashlib.sha256(data).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--identity',type=Path,required=True)
    p.add_argument('--bundle',type=Path,required=True)
    p.add_argument('--desktop',type=Path,required=True)
    p.add_argument('--firmware',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();identity=json.loads(a.identity.read_text(encoding='utf-8'))
    version=identity['version']; checks=[]
    def check(value,name):
        if not value:raise RuntimeError(name)
        checks.append(name)
    exe=a.desktop/'TempHumi.exe'
    check(sha(exe.read_bytes())==identity['qt_exe_sha256'],'Recorded desktop EXE hash')
    check(version.encode('utf-16-le') in exe.read_bytes(),'Windows product version text in EXE')
    pe_version=None
    if os.name=='nt':
        literal=str(exe.resolve()).replace("'","''")
        script="$v=(Get-Item -LiteralPath '"+literal+"').VersionInfo\n@{file=$v.FileVersion;product=$v.ProductVersion}|ConvertTo-Json -Compress"
        pe_version=json.loads(subprocess.check_output(['powershell','-NoProfile','-Command',script],text=True))
        expected='.'.join(str(int(x)) for x in version.lstrip('v').split('.'))+'.0'
        check(pe_version['file']==expected,'Windows PE FileVersion resource')
        check(pe_version['product']==version,'Windows PE ProductVersion resource')
    firmware={s:a.firmware/f'TempHumi_{version}_mcu.{s}' for s in ['bin','hex']}
    for suffix,path in firmware.items():
        check(sha(path.read_bytes())==identity['mcu_'+suffix+'_sha256'],'Recorded MCU '+suffix+' hash')
    check(version.encode() in firmware['bin'].read_bytes(),'Unified version present in firmware')
    check(firmware['bin'].stat().st_size<=65536,'Fits actual 64KB target')
    manifest=json.loads((a.desktop/'release_manifest.json').read_text(encoding='utf-8'))
    check(manifest['version']==version and not manifest['unresolved'],'Desktop version and no unresolved PE imports')
    for rel,item in manifest['payload'].items():
        path=a.desktop/rel
        check(path.is_file() and path.stat().st_size==item['bytes'] and sha(path.read_bytes())==item['sha256'],'Desktop payload '+rel)
    check(sha(a.bundle.read_bytes())==identity['bundle']['sha256'],'Recorded integrated bundle hash')
    with zipfile.ZipFile(a.bundle) as z:
        names=z.namelist()
        check(len(names)==len(set(names)),'No duplicate ZIP members')
        check(z.testzip() is None,'Every ZIP member CRC')
        check(all(not PurePosixPath(n).is_absolute() and '..' not in PurePosixPath(n).parts and '\\' not in n for n in names),'Safe portable archive paths')
        check(not any(any(t in n.lower() for t in ['private_inputs','session_','.xlsx','uid.bin','before_v','demand/']) for n in names),'No original data, private experiment or backup member')
        desktop_members=[n for n in names if '/desktop/' in '/'+n]
        for n in desktop_members:
            rel=('/'+n).split('/desktop/',1)[1];path=a.desktop/rel
            check(path.is_file() and z.read(n)==path.read_bytes(),'Bundle desktop byte identity '+rel)
        for suffix,path in firmware.items():
            members=[n for n in names if n.endswith('/'+path.name)]
            check(len(members)==1 and z.read(members[0])==path.read_bytes(),'Bundle MCU '+suffix+' byte identity')
        check(any(n.endswith('ACCEPTANCE.txt') for n in names),'Acceptance/accuracy boundary included')
    result={'pass':True,'version':version,'identity_source':str(a.identity),'bundle':str(a.bundle),
            'checks':checks,'check_count':len(checks),'zip_entries':len(names),'windows_PE_version':pe_version,
            'hardware_scope':'Reuses original version-specific program/readback/UART evidence; no historical firmware reflash',
            'accuracy':'NOT INDEPENDENTLY VERIFIED','read_only':True}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='checks'},indent=2))
if __name__=='__main__':main()
