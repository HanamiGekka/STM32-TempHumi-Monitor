"""Package the existing Qt 5.14.2 Release kit with explicit dependency evidence."""
from pathlib import Path
from desktop_version import desktop_version
import argparse,hashlib,json,subprocess,shutil,sys,zipfile,os
from datetime import datetime,timezone
ROOT=Path(__file__).resolve().parents[1]
from run_software_demo import environment
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(); p.add_argument('--build-summary',type=Path,required=True); p.add_argument('--evidence',type=Path,required=True); p.add_argument('--destination',type=Path); p.add_argument('--zip',action='store_true'); a=p.parse_args()
    version=desktop_version(); data=json.loads(a.build_summary.read_text(encoding='utf-8'))
    if data['version']!=version:raise RuntimeError('Desktop version differs from build manifest')
    build=next(x for x in data['builds'] if x['configuration']=='release')
    if not data['pass'] or data['version']!=version or not build['pass']: raise RuntimeError('Unverified Release build')
    for name,digest in data['source_hashes'].items():
        if sha(ROOT/name)!=digest: raise RuntimeError('Source changed after the verified build: '+name)
    exe=Path(build['exe']); assert sha(exe)==build['exe_sha256'],'EXE changed after build check'
    target=(a.destination or ROOT/'dist'/version).resolve(); target.mkdir(parents=True,exist_ok=False); a.evidence.mkdir(parents=True,exist_ok=True)
    qt,gcc,env=environment(); shutil.copy2(exe,target/'TempHumi.exe'); shutil.copy2(ROOT/'UIDesktop/resources/temphumi.ico',target/'TempHumi.ico')
    command=[str(qt/'bin/windeployqt.exe'),'--release','--compiler-runtime','--no-translations','--no-patchqt',str(target/'TempHumi.exe')]
    deployed=subprocess.run(command,env=env,cwd=target,capture_output=True,timeout=180)
    (a.evidence/'windeployqt.log').write_bytes(deployed.stdout+deployed.stderr)
    recovery=None
    if deployed.returncode:
        if b'Unable to find the platform plugin.' not in deployed.stdout+deployed.stderr:
            raise RuntimeError('windeployqt failed; inspect its preserved log')
        recovery_command=[str(qt/'bin/windeployqt.exe'),'--release','--no-plugins','--no-compiler-runtime','--no-translations','--no-patchqt',str(target/'TempHumi.exe')]
        recovered=subprocess.run(recovery_command,env=env,cwd=target,capture_output=True,timeout=180)
        (a.evidence/'windeployqt_recovery.log').write_bytes(recovered.stdout+recovered.stderr)
        recovery={'command':recovery_command,'exit':recovered.returncode,'reason':'installed deployment tool did not find the existing platform plugin; copy same-kit dependencies explicitly; this is not an automatic deployment PASS'}
        if recovered.returncode and b'Unable to find the platform plugin.' not in recovered.stdout+recovered.stderr:
            raise RuntimeError('windeployqt library deployment failed unexpectedly')
        for name in ['Qt5Core.dll','Qt5Gui.dll','Qt5Widgets.dll','Qt5SerialPort.dll','Qt5PrintSupport.dll','libEGL.dll','libGLESv2.dll']:
            shutil.copy2(qt/'bin'/name,target/name)
        for category,names in {'platforms':['qwindows.dll'],'printsupport':['windowsprintersupport.dll'],'imageformats':['qico.dll','qjpeg.dll','qgif.dll']}.items():
            (target/category).mkdir(exist_ok=True)
            for name in names:
                source=qt/'plugins'/category/name
                if not source.is_file(): raise RuntimeError('Missing same-kit plugin '+str(source))
                shutil.copy2(source,target/category/name)
    # The installed Qt deployment tool cannot discover this separately installed
    # MinGW kit on some machines. These exact same-kit runtime files are required.
    origin={}
    for name in ['libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll']:
        source=gcc/name
        if not source.is_file(): raise RuntimeError('Missing same-kit runtime '+str(source))
        if (target/name).exists() and sha(target/name)!=sha(source): raise RuntimeError('Mismatched runtime '+name)
        shutil.copy2(source,target/name); origin[name]=str(source)
    required=['Qt5Core.dll','Qt5Gui.dll','Qt5Widgets.dll','Qt5SerialPort.dll','Qt5PrintSupport.dll','platforms/qwindows.dll']
    for name in required:
        if not (target/name).is_file(): raise RuntimeError('Deployment missing '+name)
    (target/'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\n',encoding='utf-8')
    (target/'README.txt').write_text(f'''TempHumi Monitor desktop {version}

Double-click TempHumi.exe. No Qt Creator or developer PATH is required.
Startup prepares SERIAL without opening a port. Use UART Settings then Draw / Start.
Help > User Guide (F1) is embedded and available offline. About reports actual observed MCU firmware separately.
The MCU source version is {data.get('firmware_source_version','UNKNOWN')}; this desktop package does not flash firmware.
Automatic recovery and Restore Confirmed Configuration are separate controls, both ON by default.
Recovery requires one trusted USB identity/location and a fresh same-MCU factory UID.
It waits 5000 ms between attempts and allows at most 12 attempts per incident, then waits for the user.
The STM32F1 debug device code may read zero in independent operation (ST ES096); the factory UID remains required.
After live TH3/MODE/STATUS/CAL checks, restoration can reapply only the previously confirmed sources and fit.
Manual source/fit edits still require Apply. An invalid REAL channel never silently becomes SIM.
Click a curve or event for persistent Inspector details; local time includes its UTC offset. Original evidence keeps UTC.
Exit asks for confirmation, default No. Yes performs End Session, completes records and releases COM.
Use Open Replay or Open File Stream explicitly for software input. SIM remains visibly labelled.
Choose Recording Location for raw/CSV/metadata; Export Session copies a stopped complete session.
Sensor accuracy remains unverified. REAL humidity hardware issues remain recorded as NA/errors.
Tested locally on Windows 11 with Qt 5.14.2 / MinGW 7.3 64-bit, not a claim covering every Windows computer.
Source/build instructions: docs/development/BUILD.md in the corresponding source checkout.
Included license texts and their source locations are indexed in vendor_notices/README.txt.
''',encoding='utf-8')
    # The installed kit keeps its notices outside the binary kit root. A glob
    # of that root alone can silently produce a package with no license texts.
    license_dir=target/'vendor_notices';notice_origins={}
    def notice(source,relative):
        if not source.is_file():raise RuntimeError('Missing required notice: '+str(source))
        destination=license_dir/relative;destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,destination)
        notice_origins['vendor_notices/'+relative]={'source':str(source),'sha256':sha(source)}
    notice(ROOT/'LICENSE','project/GPL-3.0.txt')
    for name in ['LGPL-3.0.txt','BSD-3-Clause.txt','Apache-2.0.txt']:
        notice(ROOT/'LICENSES'/name,'project/'+name)
    qt_source=qt.parent/'Src'/'qtbase'
    for name in ['LICENSE.GPL3','LICENSE.LGPL3','LICENSE.GPL3-EXCEPT']:
        notice(qt_source/name,'qt/'+name)
    notice(qt_source/'src/3rdparty/angle/LICENSE','qt/ANGLE-LICENSE.txt')
    for component in ['gcc','winpthreads','mingw-w64']:
        notice_root=gcc.parent/'licenses'/component
        files=sorted(p for p in notice_root.rglob('*') if p.is_file()) if notice_root.is_dir() else []
        if not files:raise RuntimeError('Missing installed runtime notices: '+str(notice_root))
        for source in files:notice(source,'mingw/'+component+'/'+source.relative_to(notice_root).as_posix())
    (license_dir/'README.txt').write_text(f'''TempHumi {version}: local build notices
Project and QCustomPlot2.1.0: GPL-3.0-or-later; QCustomPlot copyright Emanuel Eichhammer.
Qt5.14.2 dynamic libraries/plugins: original Qt source license texts; EGL/GLES include ANGLE.
MinGW7.3 runtime: original installed GCC/runtime exception, winpthreads and mingw-w64 notices.
ST HAL/Arm CMSIS firmware terms: project BSD/Apache texts, with original source headers retained.
Each copied text is byte-identical to its project/installed-kit origin; release_manifest.json
records the notice source and SHA-256. This index is provenance, not a public publication claim.
Corresponding project build source: commit {data['head']} in the source checkout.
Build reproduction: docs/development/BUILD.md. Qt source: {qt_source.parent}.
Existing source attribution and per-file terms remain authoritative. No private session/board data.
''',encoding='utf-8')
    imports={}; unresolved=[]
    for binary in sorted(target.rglob('*')):
        if binary.suffix.lower() not in ['.exe','.dll']: continue
        result=subprocess.run([str(gcc/'objdump.exe'),'-p',str(binary)],capture_output=True,check=True)
        names=[line.split('DLL Name:')[1].strip() for line in result.stdout.decode(errors='replace').splitlines() if 'DLL Name:' in line]
        imports[str(binary.relative_to(target))]=names
        for name in names:
            supplied=any(x.name.lower()==name.lower() for x in target.rglob('*.dll'))
            system=(Path(os.environ['SystemRoot'])/'System32'/name).is_file() or name.lower().startswith(('api-ms-win-','ext-ms-win-'))
            if not supplied and not system: unresolved.append({'binary':str(binary.relative_to(target)),'dependency':name})
    for binary in target.rglob('*.dll'):
        relative=binary.relative_to(target)
        source=next((x for x in [qt/'bin'/binary.name,qt/'plugins'/relative,gcc/binary.name] if x.is_file() and sha(x)==sha(binary)),None)
        if source is None: raise RuntimeError('Dependency does not match installed kit: '+str(relative))
        origin[str(relative)]=str(source)
    manifest={'version':version,'created_at':datetime.now(timezone.utc).isoformat(),'build_head':data['head'],'exe_sha256':sha(target/'TempHumi.exe'),'source_hashes':data['source_hashes'],'kit':str(qt),'mingw':str(gcc),'windeployqt_exit':deployed.returncode,'windeployqt_command':command,'recovery':recovery,'runtime_origins':origin,'notice_origins':notice_origins,'packaging_tool_sha256':sha(Path(__file__)),'imports':imports,'unresolved':unresolved,'payload':{str(x.relative_to(target)):{'bytes':x.stat().st_size,'sha256':sha(x)} for x in sorted(target.rglob('*')) if x.is_file()}}
    manifest['firmware_source_version']=data.get('firmware_source_version','UNKNOWN')
    manifest['firmware_programmed_by_package']=False
    (target/'release_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (a.evidence/'deployment_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    if unresolved: raise RuntimeError('Unresolved DLL dependency; see deployment_manifest.json')
    if a.zip:
        destination=target.parent/f'TempHumi_{version}_win64.zip'
        if destination.exists(): raise RuntimeError('ZIP destination already exists')
        with zipfile.ZipFile(destination,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as out:
            for file in sorted(target.rglob('*')):
                if file.is_file(): out.write(file,version+'/'+file.relative_to(target).as_posix())
        (a.evidence/'package_hashes.json').write_text(json.dumps({'version':version,'exe_sha256':sha(target/'TempHumi.exe'),'zip':str(destination),'zip_sha256':sha(destination)},indent=2),encoding='utf-8')
    print(json.dumps({'version':version,'destination':str(target),'exe_sha256':manifest['exe_sha256'],'windeployqt_exit':deployed.returncode,'unresolved':unresolved,'file_count':len(manifest['payload'])+1}),flush=True)
    return 0
if __name__=='__main__': raise SystemExit(main())
