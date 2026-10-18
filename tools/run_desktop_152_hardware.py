"""Controlled real UART recovery/restore; optional identity-gated reset, never flashes."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, threading, time
from run_software_demo import environment
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--objects',type=Path,required=True);p.add_argument('--build-summary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--port',required=True)
    p.add_argument('--expected-firmware',required=True,help='Expected actual MCU version, independently checked via STATUS')
    p.add_argument('--software-reset',action='store_true',help='Explicit controlled MCU reset after verified readiness; never writes Flash')
    p.add_argument('--programming-evidence',type=Path,help='Verified same-board UID/probe hashes required for optional software reset');a=p.parse_args()
    if a.software_reset:assert a.programming_evidence,'Identity evidence required for explicit reset'
    summary=json.loads(a.build_summary.read_text(encoding='utf-8'));assert summary['pass']
    for rel,digest in summary['source_hashes'].items():assert hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()==digest,rel
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);qt,gcc,env=environment()
    for key in ['QT_SCALE_FACTOR','QT_SCREEN_SCALE_FACTORS','QT_FONT_DPI','QT_QPA_PLATFORM','QT_AUTO_SCREEN_SCALE_FACTOR']:env.pop(key,None)
    objects=[x for x in a.objects.resolve().glob('*.o') if x.name!='main.o']
    pro=out/'probe.pro';pro.write_text('QT += core gui widgets serialport printsupport testlib\nCONFIG += console debug c++11\nCONFIG -= release app_bundle\nTARGET=hardware152\nINCLUDEPATH += '+
      ' '.join('"'+(ROOT/x).as_posix()+'"' for x in ['UIDesktop','UIDesktop/config','UIDesktop/plot'])+
      '\nSOURCES += "'+(ROOT/'tools/tests/desktop152_hardware_probe.cpp').as_posix()+'"\nLIBS += '+' '.join('"'+x.as_posix()+'"' for x in objects)+'\n',encoding='utf-8')
    for label,cmd in [('qmake',[qt/'bin/qmake.exe',pro]),('build',[gcc/'mingw32-make.exe','-j2','debug'])]:
        r=subprocess.run([str(x) for x in cmd],cwd=out,env=env,capture_output=True,timeout=180);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        assert r.returncode==0 and b'warning:' not in (r.stdout+r.stderr).lower(),label
    stop=threading.Event();reset_result={};worker=None
    if a.software_reset:
        identity=json.loads(a.programming_evidence.read_text(encoding='utf-8'));assert identity['pass'] and identity['programmed_prefix_equal']
        def reset_target():
            try:
                for _ in range(600):
                    if stop.is_set():return
                    if (out/'software_reset_ready.json').exists():
                        try:
                            ready=json.loads((out/'software_reset_ready.json').read_text(encoding='utf-8'));break
                        except (OSError,json.JSONDecodeError):pass
                    time.sleep(.1)
                else:raise TimeoutError('No verified GUI reset readiness')
                assert ready['serial_open'] and ready['firmware_current_connection_verified'] and ready['observed_firmware']==a.expected_firmware
                cli=Path(r'E:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe')
                private=out/'reset_private';private.mkdir(exist_ok=False)
                def command(args,name):
                    p=subprocess.run([str(x) for x in args],capture_output=True,timeout=25);(private/name).write_bytes(p.stdout+p.stderr)
                    assert p.returncode==0,name;return (p.stdout+p.stderr).decode(errors='replace')
                listing=command([cli,'-l','st-link'],'probes.log')
                candidates=[s for s in re.findall(r'ST-LINK SN\s*:\s*([0-9A-Fa-f]+)',listing) if hashlib.sha256(s.encode()).hexdigest()==identity['probe_sn_sha256']]
                assert len(candidates)==1,'Exact trusted probe required';connection=[cli,'-c','port=SWD','sn='+candidates[0],'freq=1000']
                uid=private/'uid.bin';command(connection+['-u','0x1FFFF7E8','12',uid],'target_identity.log')
                assert hashlib.sha256(uid.read_bytes()).hexdigest()==identity['target_uid_sha256'],'Exact trusted target required'
                if stop.is_set():raise RuntimeError('GUI test ended; cancel delayed reset')
                command(connection+['-rst'],'software_reset.log')
                reset_result.update(pass_=True,reset_operations=1,flash_writes=0,scope='Controlled MCU software reset, not USB contact/power-cycle')
            except Exception as error:reset_result.update(pass_=False,error=repr(error))
        worker=threading.Thread(target=reset_target);worker.start()
    try:
        args=[out/'debug/hardware152.exe',a.port,summary['version'],a.expected_firmware]+(['--software-reset-test'] if a.software_reset else [])
        r=subprocess.run(args,cwd=out,env=env,capture_output=True,timeout=240)
    finally:
        stop.set()
        if worker:worker.join(timeout=30)
    (out/'run.log').write_bytes(r.stdout+r.stderr);result=json.loads(r.stdout)
    result['production_input_hashes']=summary['source_hashes']
    if a.software_reset:
        result['controlled_reset']=reset_result
        if not reset_result.get('pass_'):result['pass']=False;result['failures'].append('Controlled-reset identity/controller failed')
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:result.get(k) for k in ['pass','checks','failures','blocked','actual_observed_firmware']},ensure_ascii=False));return 0 if result['pass'] else (r.returncode or 1)
if __name__=='__main__':raise SystemExit(main())
