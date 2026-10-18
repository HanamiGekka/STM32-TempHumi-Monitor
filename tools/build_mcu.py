"""Build retained CubeIDE configurations in fresh folders without altering caches."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
from datetime import datetime,timezone
from update_project_version import update_project_version
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-root',type=Path,required=True)
    p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
    update_project_version()
    cube=Path(os.environ.get('STM32CUBEIDE_ROOT',r'E:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE'))
    gcc=next(cube.glob('plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.*/tools/bin'))
    make=next(cube.glob('plugins/com.st.stm32cube.ide.mcu.externaltools.make.win32_*/tools/bin/make.exe'))
    env=os.environ.copy();env['PROJECT_ROOT']=ROOT.as_posix();env['PATH']=str(gcc)+os.pathsep+str(make.parent)+os.pathsep+env['PATH']
    protected={str(f.relative_to(ROOT)):sha(f) for name in ['Debug','Release'] for f in (ROOT/name).rglob('*') if f.is_file()}
    sources=[f for name in ['Core','Drivers'] for f in (ROOT/name).rglob('*') if f.is_file() and f.suffix in ['.c','.h','.s']]+[ROOT/'STM32F103C8TX_FLASH.ld',ROOT/'VERSION']
    result={'version':(ROOT/'VERSION').read_text().strip(),'build_baseline_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            'source_hashes':{str(f.relative_to(ROOT)):sha(f) for f in sources},'toolchain':str(gcc),'builds':[],'at':datetime.now(timezone.utc).isoformat()}
    for config in ['Debug','Release']:
        original=ROOT/config;folder=a.build_root/config;folder.mkdir(parents=True,exist_ok=False);metadata={}
        for f in original.rglob('*'):
            if f.is_file() and (f.suffix=='.mk' or f.name in ['makefile','objects.list']):
                rel=f.relative_to(original);target=folder/rel;target.parent.mkdir(parents=True,exist_ok=True)
                text=f.read_text(encoding='utf-8-sig')
                for name in ['Core','Drivers']:
                    text=text.replace('../'+name+'/',ROOT.as_posix()+'/'+name+'/')
                text=text.replace('../makefile.',ROOT.as_posix()+'/makefile.')
                target.write_text(text,encoding='utf-8');metadata[str(rel)]={'original_sha256':sha(f),'derived_sha256':sha(target)}
        proc=subprocess.run([str(make),'-j2','all'],cwd=folder,env=env,capture_output=True,timeout=240)
        log=a.evidence/(config.lower()+'_build.log');log.write_bytes(proc.stdout+proc.stderr)
        text=(proc.stdout+proc.stderr).decode(errors='replace');elf=folder/'STM32_TempHumi_Monitor.elf'
        item={'configuration':config,'exit':proc.returncode,'warnings':re.findall(r'^.*warning:.*$',text,re.M|re.I),
              'errors':re.findall(r'^.*(?:error:|Error ).*$',text,re.M),'elf':str(elf),'metadata':metadata}
        if proc.returncode==0 and elf.is_file():
            binary=folder/'STM32_TempHumi_Monitor.bin';hexfile=folder/'STM32_TempHumi_Monitor.hex'
            subprocess.run([str(gcc/'arm-none-eabi-objcopy.exe'),'-O','binary',str(elf),str(binary)],check=True)
            subprocess.run([str(gcc/'arm-none-eabi-objcopy.exe'),'-O','ihex',str(elf),str(hexfile)],check=True)
            size=subprocess.check_output([str(gcc/'arm-none-eabi-size.exe'),str(elf)],text=True)
            item.update(elf_sha256=sha(elf),bin=str(binary),bin_sha256=sha(binary),bin_bytes=binary.stat().st_size,hex=str(hexfile),hex_sha256=sha(hexfile),size=size)
        item['pass']=proc.returncode==0 and elf.is_file() and not item['warnings'] and not item['errors'] and item.get('bin_bytes',65537)<=65536
        result['builds'].append(item)
        if not item['pass']:break
    result['retained_caches_unchanged']=all((ROOT/n).is_file() and sha(ROOT/n)==h for n,h in protected.items())
    result['pass']=len(result['builds'])==2 and all(b['pass'] for b in result['builds']) and result['retained_caches_unchanged']
    (a.evidence/'mcu_build_summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='source_hashes'},indent=2),flush=True)
    return 0 if result['pass'] else 1
if __name__=='__main__':raise SystemExit(main())
