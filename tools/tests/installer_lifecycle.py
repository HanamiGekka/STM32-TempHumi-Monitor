"""Actual current-user install/start/replay/uninstall acceptance, without COM access.

Uses the production Setup in two empty destinations. Only this harness's synthetic
unowned-file sentinel is moved after uninstall; actual user settings/data are read-only.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse, csv, ctypes, hashlib, json, os, re, shutil, subprocess, sys, time, winreg
import psutil
import pythoncom
from win32com.shell import shell

ROOT=Path(__file__).resolve().parents[2]
PRODUCT_KEY=r'Software\Microsoft\Windows\CurrentVersion\Uninstall\{3CF43736-ED17-4989-B1C6-E6D143B82CC8}_is1'
NATIVE_KEY=r'Software\TempHumi\TempHumi Monitor'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path,value):path.write_bytes((json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
def registry(path):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,path) as key:
            subs,count,_=winreg.QueryInfoKey(key);result={}
            for i in range(count):
                name,value,kind=winreg.EnumValue(key,i)
                result['value:'+name]=[kind,value.hex() if isinstance(value,bytes) else value]
            for i in range(subs):
                name=winreg.EnumKey(key,i);result['key:'+name]=registry(path+'\\'+name)
            return result
    except FileNotFoundError:return None

def powershell(script):
    data=subprocess.check_output(['powershell','-NoProfile','-Command',script],cwd=ROOT)
    return json.loads(data.decode('utf-8-sig'))

def shortcut_details(path):
    # WScript.Shell's legacy TargetPath getter loses non-ACP characters. Read
    # the actual Unicode Shell Link interface used by Windows instead.
    link=pythoncom.CoCreateInstance(shell.CLSID_ShellLink,None,pythoncom.CLSCTX_INPROC_SERVER,shell.IID_IShellLink)
    link.QueryInterface(pythoncom.IID_IPersistFile).Load(str(path))
    return {'target':link.GetPath(shell.SLGP_RAWPATH)[0],'working':link.GetWorkingDirectory(),
            'icon':link.GetIconLocation()[0]}

def modules(pid):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);psapi=ctypes.WinDLL('psapi',use_last_error=True)
    kernel.OpenProcess.restype=ctypes.c_void_p
    kernel.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32]
    kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    psapi.EnumProcessModulesEx.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32]
    psapi.GetModuleFileNameExW.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_uint32]
    handle=kernel.OpenProcess(0x0410,False,pid);assert handle
    try:
        values=(ctypes.c_void_p*2048)();needed=ctypes.c_uint32()
        assert psapi.EnumProcessModulesEx(handle,values,ctypes.sizeof(values),ctypes.byref(needed),3)
        paths=[]
        for module in values[:needed.value//ctypes.sizeof(ctypes.c_void_p)]:
            name=ctypes.create_unicode_buffer(32768)
            if psapi.GetModuleFileNameExW(handle,module,name,len(name)):paths.append(Path(name.value))
        return paths
    finally:kernel.CloseHandle(handle)

def owned_processes(exe):
    result=[]
    for process in psutil.process_iter(['pid','name']):
        if (process.info['name'] or '').lower()!='temphumi.exe':continue
        try:
            if Path(process.exe()).resolve()==exe.resolve():result.append(process)
        except (psutil.NoSuchProcess,psutil.AccessDenied):pass
    return result

def run_app(exe,case,mode,environment,system,shortcut=None):
    location=case/mode;location.mkdir();status=location/'status.json'
    args=['--quit-after','6','--settings-file',str(location/'preferences.ini'),
          '--record-dir',str(location/'sessions'),'--status-file',str(status)]
    if mode=='direct':args+=['--user-guide']
    if mode=='recording':args+=['--input-mode','REPLAY','--input',str(ROOT/'tools/qt_sim_package/samples/TH3_sample.txt'),'--autostart']
    if shortcut:
        assert not owned_processes(exe)
        previous=dict(os.environ);working=Path.cwd()
        try:
            os.environ.clear();os.environ.update(environment);os.chdir(system)
            os.startfile(str(shortcut),'open',arguments=subprocess.list2cmdline(args),show_cmd=7)
        finally:os.environ.clear();os.environ.update(previous);os.chdir(working)
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            matches=owned_processes(exe)
            if len(matches)==1:break
            time.sleep(.1)
        assert len(matches)==1,'Shortcut did not start exactly one installed application'
        observed=matches[0];time.sleep(.7);loaded=modules(observed.pid)
        assert observed.environ()['PATH']==str(system),'Shortcut did not inherit isolated runtime environment'
        exit_code=observed.wait(timeout=15)
    else:
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=7
        with (location/'stdout.log').open('wb') as stdout,(location/'stderr.log').open('wb') as stderr:
            process=subprocess.Popen([str(exe),*args],cwd=system,env=environment,stdout=stdout,stderr=stderr,startupinfo=startup)
            time.sleep(1);loaded=modules(process.pid);exit_code=process.wait(timeout=20)
        assert not (location/'stderr.log').read_bytes()
    assert exit_code==0 and status.is_file(),mode
    state=json.loads(status.read_text(encoding='utf-8-sig'))
    manifest=json.loads((exe.parent/'release_manifest.json').read_text(encoding='utf-8-sig'))
    assert sha(exe)==manifest['exe_sha256']
    assert state['software_version']==manifest['version'] and state['git_head']==manifest['build_head']
    assert state['about_text'].startswith('TempHumi Monitor '+manifest['version']+'\nTH3 | Qt 5.14.2')
    assert not state['recording'] and not state['error']
    if state['input_mode']=='REPLAY':
        assert not state['active_port'] and state['connection_state']=='DISCONNECTED'
    else:
        assert state['serial_open'] is False
    if mode=='direct':assert state['help_resource_loaded'] and state['help_embedded_available']
    if mode=='recording':
        records=list((location/'sessions').glob('*/metadata.json'));assert len(records)==1
        metadata=json.loads(records[0].read_text());assert metadata['parsed_total_count']>0
        assert not metadata['active'] and metadata['stop_reason']=='end_session' and not metadata['recording_error']
        assert 'REPLAY' in metadata['classification'].upper()
    required={'qt5core.dll','qt5gui.dll','qt5widgets.dll','qt5serialport.dll','qt5printsupport.dll',
              'libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll','qwindows.dll'}
    selected=[path for path in loaded if path.name.lower() in required]
    assert required=={path.name.lower() for path in selected}
    assert all(path.is_relative_to(exe.parent) for path in selected)
    return {'mode':mode,'pass':True,'source_commit':state['git_head'],'version':state['software_version'],
            'offline_help':bool(state.get('help_resource_loaded')),'serial_open':False,'runtime_modules_local':len(selected),
            'recording_directory_writable':mode=='recording','normal_exit':True,'environment':'System32-only PATH/cwd, isolated preferences'}

def run_hardware(exe,case,environment,system,port):
    location=case/'hardware';location.mkdir();uid=os.environ['TEMPHUMI_TEST_EXPECTED_UID_SHA256']
    command=['powershell','-NoProfile','-File',str(ROOT/'tools/capture_uart_control.ps1'),
             '-Destination',str(location/'readonly_before.log'),'-ExpectedVersion','v010.17.1',
             '-ExpectedIdentitySha256',uid,'-SelectedPort',port,'-Seconds','8']
    before=subprocess.run(command,capture_output=True,timeout=30)
    (location/'readonly_controller.log').write_bytes(before.stdout+before.stderr);assert before.returncode==0
    raw=(location/'readonly_before.log').read_bytes()
    modes=re.findall(rb'RSP,MODE,1,\d+,(REAL|SIM),(REAL|SIM),\d+,(REAL|SIM),(REAL|SIM)',raw)
    profiles=re.findall(rb'RSP,CAL,1,(\d+),(\d+)',raw)
    assert modes and profiles and profiles[-1][0]==profiles[-1][1]
    expected_sources=[x.decode() for x in modes[-1][2:]];expected_profile=int(profiles[-1][1])
    state_path=location/'status.json'
    command=[str(exe),'--input-mode','SERIAL','--serial',port,'--baud','115200','--autostart','--test-commands',
             '--quit-after','70','--settings-file',str(location/'preferences.ini'),
             '--record-dir',str(location/'sessions'),'--status-file',str(state_path)]
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=7
    print('Installed application: 70-second same-board UART capture started',flush=True)
    with (location/'stdout.log').open('wb') as stdout,(location/'stderr.log').open('wb') as stderr:
        process=subprocess.Popen(command,cwd=system,env=environment,stdout=stdout,stderr=stderr,startupinfo=startup)
        time.sleep(1);loaded=modules(process.pid);code=process.wait(timeout=95)
    assert code==0 and not (location/'stderr.log').read_bytes()
    state=json.loads(state_path.read_text());assert state['mcu_identity_verified'] and state['mcu_identity_hash']==uid
    assert state['observed_firmware']=='v010.17.1' and state['git_head']=='f3d11011a3a6b42390839f16cd63f0f16abbdbdd'
    assert not state['serial_open'] and not state['recording'] and not state['error']
    assert [state['frame_temperature_source'],state['frame_humidity_source']]==expected_sources
    assert state['calibration_frame_profile']==expected_profile
    sessions=list((location/'sessions').glob('*/parsed.csv'));assert len(sessions)==1
    with sessions[0].open(encoding='utf-8-sig',newline='') as f:rows=[x for x in csv.DictReader(f) if x['parse_status']=='PARSED']
    assert len(rows)>=60
    stamps=[int(x['source_timestamp_ms']) for x in rows];deltas=[(b-a)&0xffffffff for a,b in zip(stamps,stamps[1:])]
    span=sum(deltas)/1000;assert span>=60 and all(0<d<2500 for d in deltas)
    assert all(x['temperature_valid']=='1' for x in rows)
    tx=[json.loads(x) for x in (sessions[0].parent/'command_tx.jsonl').read_text().splitlines() if x]
    assert all(not bytes.fromhex(x['bytes_hex']).startswith((b'CMD,SET,',b'CMD,CAL,')) for x in tx)
    assert all(path.is_relative_to(exe.parent) for path in loaded if path.name.lower().startswith('qt5'))
    return {'pass':True,'board':'THM-BOARD-01','identity_sha256':uid,'firmware':'v010.17.1',
            'frames':len(rows),'source_span_seconds':span,'sources':expected_sources,'profile':expected_profile,
            'sources_and_profile_unchanged':True,'changing_commands':0,'firmware_writes':0,'End_Session':True,
            'session_parent':str(location/'sessions'),'scope':'Actual installed EXE/UART; numeric OLED audited separately, no optical or independent accuracy claim'}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--identity',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--private',type=Path,required=True)
    parser.add_argument('--hardware-port',help='Explicit authorized same-board UART acceptance; no reset/flash/source changes')
    args=parser.parse_args();args.private=args.private.resolve();args.output=args.output.resolve()
    identity=json.loads(args.identity.read_text());setup=Path(identity['setup_path'])
    assert sha(setup)==identity['setup_sha256']
    args.private.mkdir(parents=True,exist_ok=False);args.output.parent.mkdir(parents=True,exist_ok=True)
    assert registry(PRODUCT_KEY) is None,'Existing user installation must not be overwritten'
    before=registry(NATIVE_KEY);save(args.private/'native_preferences_before.json',before)
    shell=powershell("[Console]::OutputEncoding=[Text.UTF8Encoding]::new();$s=New-Object -ComObject WScript.Shell;@{desktop=$s.SpecialFolders.Item('Desktop');programs=$s.SpecialFolders.Item('Programs')}|ConvertTo-Json -Compress")
    desktop_link=Path(shell['desktop'])/'TempHumi Monitor.lnk'
    menu=Path(shell['programs'])/'TempHumi Monitor';menu_link=menu/'TempHumi Monitor.lnk'
    assert not desktop_link.exists() and not menu.exists(),'Existing user shortcuts must be preserved'
    system=Path(os.environ['SystemRoot'])/'System32'
    environment={k:v for k,v in os.environ.items() if not k.upper().startswith('QT_') and k.upper() not in ['QTDIR','TEMPHUMI_SETTINGS_PATH','TEMP_HUMI_QT_ROOT']}
    environment['PATH']=str(system)
    targets=[('default',Path(r'E:\Program Files\TempHumi Monitor'),True),
             ('alternate',ROOT/'build_verify/v010171_final_closeout/安装测试 目录',False)]
    results=[]
    for label,target,desktop_expected in targets:
        assert not target.exists() and not target.is_symlink() and not target.is_junction(),target
        case=args.private/label;case.mkdir()
        command=[str(setup),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',
                 '/NOCLOSEAPPLICATIONS','/NORESTARTAPPLICATIONS','/LOG='+str(case/'install.log'),
                 '/TASKS='+('desktopicon' if desktop_expected else '')]
        if label=='alternate':command+=['/DIR='+str(target)]
        process=subprocess.run(command,capture_output=True,timeout=60)
        (case/'install_controller.log').write_bytes(process.stdout+process.stderr)
        assert process.returncode==0,(label,process.returncode)
        assert target.is_dir() and not target.is_junction() and not target.is_symlink()
        for item in identity['payload']:assert sha(target/item['relative'])==item['sha256'],item['relative']
        assert sha(target/'TempHumi.exe')==identity['portable_exe_sha256']
        registered=registry(PRODUCT_KEY);assert registered is not None
        assert registered['value:DisplayVersion'][1]==identity['version']
        assert Path(registered['value:InstallLocation'][1]).resolve()==target.resolve()
        assert menu_link.is_file() and desktop_link.exists()==desktop_expected
        assert not owned_processes(target/'TempHumi.exe'),'Setup must not autolaunch or connect the application'
        shortcut_data=shortcut_details(menu_link)
        assert Path(shortcut_data['target']).resolve()==(target/'TempHumi.exe').resolve()
        starts=[run_app(target/'TempHumi.exe',case,'direct',environment,system),
                run_app(target/'TempHumi.exe',case,'shortcut',environment,system,menu_link),
                run_app(target/'TempHumi.exe',case,'recording',environment,system)]
        hardware=run_hardware(target/'TempHumi.exe',case,environment,system,args.hardware_port) if label=='default' and args.hardware_port else None
        sentinel=target/'USER_DATA_NOT_INSTALLED_BY_SETUP.csv'
        sentinel_bytes=b'SYNTHETIC uninstall retention fixture\nvalue,1\n';sentinel.write_bytes(sentinel_bytes)
        assert registry(NATIVE_KEY)==before,'Isolated launches changed real user preferences'
        uninstaller=target/'unins000.exe';assert uninstaller.is_file() and uninstaller.resolve().parent==target.resolve()
        uninstall=subprocess.run([str(uninstaller),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/LOG='+str(case/'uninstall.log')],capture_output=True,timeout=60)
        (case/'uninstall_controller.log').write_bytes(uninstall.stdout+uninstall.stderr)
        assert uninstall.returncode==0
        # The registered launcher can exit before its temporary uninstaller finishes.
        deadline=time.monotonic()+15
        while time.monotonic()<deadline and ((target/'TempHumi.exe').exists() or uninstaller.exists() or registry(PRODUCT_KEY) is not None):time.sleep(.2)
        assert not (target/'TempHumi.exe').exists() and not uninstaller.exists()
        assert all(not (target/item['relative']).exists() for item in identity['payload'])
        assert not menu_link.exists() and not desktop_link.exists() and registry(PRODUCT_KEY) is None
        assert sentinel.read_bytes()==sentinel_bytes and registry(NATIVE_KEY)==before
        leftovers=[p for p in target.rglob('*') if p.is_file()];assert leftovers==[sentinel],leftovers
        # Preserve this test-created sentinel as evidence, then remove only the empty test destination.
        assert sentinel.resolve().parent==target.resolve()
        shutil.move(str(sentinel),str(case/'retained_unowned_user_data.csv'))
        assert not list(target.iterdir()),'Do not recursively remove unknown leftovers'
        target.rmdir()
        results.append({'case':label,'pass':True,'installed_directory':str(target),'default_directory_used':label=='default',
                        'unicode_and_space_path':label=='alternate','payload_files_verified':len(identity['payload']),
                        'desktop_shortcut_created':desktop_expected,'start_menu_shortcut_created':True,'starts':starts,
                        'actual_hardware':hardware,
                        'Windows_uninstall_registration':True,'uninstall_exit':0,'registered_program_files_removed':True,
                        'unowned_data_preserved':True,'synthetic_data_archived_privately':True,'native_user_preferences_unchanged':True})
        print(json.dumps({'case':label,'pass':True,'starts':len(starts),'uninstall_preserved_unowned_data':True}),flush=True)
    after=registry(NATIVE_KEY);save(args.private/'native_preferences_after.json',after);assert before==after
    result={'at':datetime.now(timezone.utc).isoformat(),'pass':True,'setup_sha256':identity['setup_sha256'],
            'version':identity['version'],'cases':results,'runtime_auto_serial_connections':0,'firmware_writes':0,
            'private_evidence':str(args.private),'actual_user_data_modified':False,'signature':'UNSIGNED, no security settings changed'}
    save(args.output,result);print(json.dumps({'pass':True,'cases':2,'independent_starts':6,'output':str(args.output)}))

if __name__=='__main__':main()
