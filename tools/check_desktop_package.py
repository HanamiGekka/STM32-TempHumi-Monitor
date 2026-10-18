"""Read-only check of an independently versioned Windows desktop directory/ZIP."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, os, subprocess, zipfile

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--desktop',type=Path,required=True);p.add_argument('--zip',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    folder=a.desktop.resolve();manifest=json.loads((folder/'release_manifest.json').read_text(encoding='utf-8'));version=manifest['version'];checks=[]
    def check(condition,label):
        if not condition:raise RuntimeError(label)
        checks.append(label)
    check(not manifest['unresolved'],'No unresolved PE imports')
    check(manifest['firmware_programmed_by_package'] is False,'No firmware programming claim')
    check(digest(folder/'TempHumi.exe')==manifest['exe_sha256'],'Build EXE digest')
    for rel,item in manifest['payload'].items():
        path=folder/rel;check(path.is_file() and path.stat().st_size==item['bytes'] and digest(path)==item['sha256'],'Payload '+rel)
    if 'notice_origins' in manifest:
        normalized_payload={name.replace('\\','/') for name in manifest['payload']}
        required={'vendor_notices/project/GPL-3.0.txt','vendor_notices/project/LGPL-3.0.txt',
                  'vendor_notices/qt/LICENSE.GPL3','vendor_notices/qt/LICENSE.LGPL3',
                  'vendor_notices/mingw/gcc/COPYING.RUNTIME','vendor_notices/mingw/winpthreads/COPYING'}
        check(required<=set(manifest['notice_origins']),'Project/Qt/MinGW notice set present')
        check((folder/'vendor_notices/README.txt').is_file(),'Notice provenance index present')
        for rel,item in manifest['notice_origins'].items():
            check(rel in normalized_payload and digest(folder/rel)==item['sha256'],'Original notice byte identity '+rel)
    check((folder/'TempHumi.ico').read_bytes()[:4]==b'\0\0\1\0','Reviewed TH ICO container')
    check(version.encode('utf-16-le') in (folder/'TempHumi.exe').read_bytes(),'PE product text')
    pe=None
    if os.name=='nt':
        literal=str(folder/'TempHumi.exe').replace("'","''")
        pe=json.loads(subprocess.check_output(['powershell','-NoProfile','-Command',"$v=(Get-Item -LiteralPath '"+literal+"').VersionInfo\n@{file=$v.FileVersion;product=$v.ProductVersion}|ConvertTo-Json -Compress"],text=True))
        check(pe['product']==version and pe['file']=='.'.join(str(int(x)) for x in version.lstrip('v').split('.'))+'.0','Numeric/Product PE resources')
    with zipfile.ZipFile(a.zip) as archive:
        names=archive.namelist();check(len(names)==len(set(names)),'Unique ZIP members');check(archive.testzip() is None,'All ZIP CRCs')
        check(all(not PurePosixPath(n).is_absolute() and '..' not in PurePosixPath(n).parts and '\\' not in n and n.startswith(version+'/') for n in names),'Safe relative versioned ZIP paths')
        check(not any(any(s in n.lower() for s in ['private_inputs','session_','.xlsx','uid.bin','demand/','.cmd']) for n in names),'No private runtime/board/cleanup artifacts')
        expected={version+'/'+x.relative_to(folder).as_posix() for x in folder.rglob('*') if x.is_file()}
        check(set(names)==expected,'ZIP and directory have identical members')
        for name in names:check(archive.read(name)==(folder/PurePosixPath(name).relative_to(version)).read_bytes(),'ZIP byte identity '+name)
    out={'pass':True,'read_only':True,'desktop_version':version,'firmware_source_version':manifest['firmware_source_version'],
         'source_commit':manifest['build_head'],'checks':checks,'check_count':len(checks),'PE':pe,'zip_entries':len(names),
         'exe_sha256':digest(folder/'TempHumi.exe'),'zip_sha256':digest(a.zip)}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in out.items() if k!='checks'},indent=2));return 0
if __name__=='__main__':raise SystemExit(main())
