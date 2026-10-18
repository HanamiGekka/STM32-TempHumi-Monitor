"""Check maintained local links, Python syntax, and the reviewed public surface."""
from pathlib import Path
import argparse, ast, json, re, hashlib, subprocess
from urllib.parse import unquote

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1]);parser.add_argument('--public',action='store_true');parser.add_argument('--output',type=Path);args=parser.parse_args()
    root=args.root.resolve();errors=[];links=0;syntax=0
    index=root/'docs/release/maintained_files.json'
    if args.public:
        allow=json.loads((root/'docs/release/public_allowlist.json').read_text(encoding='utf-8'));files=allow['files']
        actual=set(subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=root).decode('utf-8').split('\0'))-{''} if (root/'.git').exists() else {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() and '.git' not in p.relative_to(root).parts}
        errors += ['Unallowlisted: '+p for p in sorted(actual-set(files))]
        errors += ['Missing allowlisted: '+p for p in sorted(set(files)-actual)]
    else:files=json.loads(index.read_text(encoding='utf-8'))
    for rel in files:
        p=root/rel
        if not p.is_file():errors.append('Missing: '+rel);continue
        if p.suffix=='.py':
            try:ast.parse(p.read_text(encoding='utf-8-sig'),filename=rel);syntax+=1
            except SyntaxError as exc:errors.append('Syntax: '+str(exc))
        if p.suffix=='.md':
            text=p.read_text(encoding='utf-8-sig')
            for target in re.findall(r'\]\(([^)]+)\)',text):
                target=target.strip().split(' "')[0].strip('<>')
                if re.match(r'^[a-z]+:',target,re.I) or target.startswith('#'):continue
                dest=(p.parent/unquote(target.split('#')[0])).resolve();links+=1
                if not dest.exists():errors.append('Broken link: '+rel+' -> '+target)
        if args.public:
            parts=Path(rel).parts
            if any(x in {'demand','temp','dist','logs','build_verify','.settings','.vscode','Board','experiments','ppt_presentation'} for x in parts) or p.suffix.lower() in {'.exe','.dll','.zip','.pdf','.docx','.pptx','.bin','.elf'} or '.pro.user' in p.name or p.suffix=='.launch':errors.append('Prohibited public file: '+rel)
            if p.suffix.lower() in {'.c','.h','.cpp','.py','.pro','.ui','.ioc','.project','.cproject','.md','.json','.txt','.bat','.ps1','.yml','.yaml','.in','.rc'}:
                text=p.read_text(encoding='utf-8-sig',errors='replace')
                patterns=[r'[A-Za-z]:[\\/]+Users[\\/]+',r'gh[pousr]_[A-Za-z0-9_]{20,}',r'-----BEGIN [A-Z ]*PRIVATE KEY-----',r'E:[\\/]+STM32[\\/]+STM32_TempHumi_Monitor']
                for pattern in patterns:
                    if re.search(pattern,text):errors.append('Private path/secret pattern: '+rel)
    nav='[English](README.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md) | [繁體中文](README.zh-TW.md)'
    for name in ['README.md','README.zh-CN.md','README.ja.md','README.zh-TW.md']:
        text=(root/name).read_text(encoding='utf-8')
        if text.splitlines()[0]!=nav:errors.append('Navigation: '+name)
        for fact in [(root/'VERSION').read_text().strip(),'TH3','SIM','1200','SHA-256','GPL-3.0']:
            if fact not in text:errors.append('README fact absent: '+name+' '+fact)
    version=(root/'VERSION').read_text().strip()
    desktop_path=root/'UIDesktop/VERSION'
    desktop_version=desktop_path.read_text(encoding='utf-8').strip() if desktop_path.exists() else version
    for name in ['README.md','README.zh-CN.md','README.ja.md','README.zh-TW.md']:
        if desktop_version not in (root/name).read_text(encoding='utf-8'):errors.append('Desktop README version absent: '+name)
    resource=root/'UIDesktop/resources/temphumi.rc'
    if resource.exists() and desktop_version not in resource.read_text(encoding='utf-8'):errors.append('Desktop resource version mismatch')
    header=root/'Core/Inc/app_project_version.h'
    if header.exists() and ('APP_PROJECT_VERSION_STR \"'+version+'\"') not in header.read_text():errors.append('MCU root version alias mismatch')
    result={'pass':not errors,'files_checked':len(files),'local_links_checked':links,'python_files_parsed':syntax,'errors':errors,'scope':'public allowlist/path/secret/syntax/maintained links' if args.public else 'maintained English guides/readmes/syntax/local links'}
    if args.output:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2));return 0 if not errors else 1
if __name__=='__main__':raise SystemExit(main())
