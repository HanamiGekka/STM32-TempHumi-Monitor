"""Current source regression using explicit synthetic/replay fixtures; no board access."""
from pathlib import Path
import argparse, json, os, subprocess, sys
ROOT=Path(__file__).resolve().parents[1]
from run_software_demo import environment
from desktop_version import desktop_version_path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root',type=Path,default=ROOT/'build_verify/regressions')
    parser.add_argument('--evidence',type=Path,default=ROOT/'build_verify/regression_evidence')
    parser.add_argument('--host-only',action='store_true',help='Portable C command/hybrid tests; GCC or CC required.')
    parser.add_argument('--desktop-objects',type=Path,help='Existing reviewed Debug object directory for GUI probes.')
    args=parser.parse_args(); build=args.build_root.resolve(); raw=args.evidence.resolve()
    build.mkdir(parents=True,exist_ok=True);raw.mkdir(parents=True,exist_ok=True)
    results=[]
    if args.host_only:
        qt=None;gcc=Path(os.environ.get('CC','gcc'));env=os.environ.copy();cc=gcc
    else:
        qt,gcc,env=environment();cc=gcc/'gcc.exe'
    def run(label,command,cwd=build,child_env=None):
        logfile=raw/(label+'.log')
        if logfile.exists():raise FileExistsError('Use a fresh evidence directory: '+str(logfile))
        p=subprocess.run([str(x) for x in command],cwd=cwd,env=child_env or env,capture_output=True,timeout=240)
        logfile.write_bytes(p.stdout+p.stderr)
        if p.returncode or b'warning:' in (p.stdout+p.stderr).lower():raise RuntimeError(label+' failed; see '+str(logfile))
        return p
    def capture(label,p):
        out=json.loads(p.stdout);results.append({'suite':label,'output':out,'pass':out.get('pass',True)})
        (raw/(label+'.json')).write_text(json.dumps(out,indent=2),encoding='utf-8')
    suffix='.exe' if os.name=='nt' else ''
    command=build/('command'+suffix)
    run('command_build',[cc,'-std=c11','-Wall','-Wextra','-Werror','-I',ROOT/'Core/Inc',ROOT/'tools/tests/command_probe.c',ROOT/'Core/Src/app_command.c','-o',command])
    capture('command',run('command_run',[command]))
    hybrid=build/('hybrid'+suffix)
    run('hybrid_build',[cc,'-std=c11','-Wall','-Wextra','-Werror','-include','stddef.h','-I',ROOT/'Core/Inc',ROOT/'tools/tests/hybrid_probe.c',*[ROOT/('Core/Src/'+x+'.c') for x in ['app_command','app_hybrid_source','app_measurement_source','app_measurement_source_sim','app_protocol','algo_sensor_convert']],'-lm','-o',hybrid])
    capture('hybrid_c',run('hybrid_c_run',[hybrid]))
    if not args.host_only:
        def qt_build(name,probe,modules,config='debug',objects=None):
            folder=build/name;folder.mkdir(exist_ok=False)
            gui=objects is not None
            src=[ROOT/'tools/tests'/probe]+[ROOT/('UIDesktop/'+x+'.cpp') for x in modules]
            pro=folder/'probe.pro'
            text=('QT += core gui widgets serialport printsupport\n' if gui else 'QT += core\nQT -= gui\n')
            text+='CONFIG += console c++11 '+config+'\nCONFIG -= app_bundle '+('release' if config=='debug' else 'debug')+'\nTARGET='+name+'\n'
            text+='INCLUDEPATH += '+' '.join('"'+p.as_posix()+'"' for p in [ROOT/'UIDesktop',ROOT/'UIDesktop/config',ROOT/'UIDesktop/plot'])+'\n'
            text+='SOURCES += '+' '.join('"'+p.as_posix()+'"' for p in src)+'\n'
            if objects:text+='LIBS += '+' '.join('"'+p.as_posix()+'"' for p in objects)+'\n'
            pro.write_text(text,encoding='utf-8')
            run(name+'_qmake',[qt/'bin/qmake.exe',pro],folder)
            run(name+'_build',[gcc/'mingw32-make.exe','-j2',config],folder)
            return folder/config/(name+'.exe')
        exe=qt_build('parser_file','parser_file_probe.cpp',['app_th3','app_line_buffer','app_file_input'])
        for case in ['parser','file','chunks']:
            output=raw/(case+'.json');p=run(case+'_run',[exe,case,build/('scratch_'+case),output]);value=json.loads(output.read_text());results.append({'suite':case,'output':value,'pass':value.get('pass',True)})
        exe=qt_build('hybrid_qt','hybrid_qt_probe.cpp',['app_th3','app_line_buffer','app_session_recorder'])
        capture('hybrid_qt',run('hybrid_qt_run',[exe,build/'th3_vectors.txt',build/'hybrid_sessions']))
        for config in ['debug','release']:
            exe=qt_build('signals_'+config,'signal_analysis_probe.cpp',['app_signal_analysis'],config)
            output=raw/('signals_'+config+'.json');run('signals_'+config+'_run',[exe,output]);value=json.loads(output.read_text());results.append({'suite':'signals_'+config,'case_count':value['case_count'],'checks_failed':value['checks_failed'],'pass':value['checks_failed']==0})
        objects=[p for p in (args.desktop_objects or build/'qt_debug/debug').resolve().glob('*.o') if p.name not in {'main.o','app_th2.o'}]
        if not any(p.name=='mainwindow.o' for p in objects):raise RuntimeError('Build desktop Debug in the same build root before GUI regression')
        exe=qt_build('desktop_probe','desktop_probe.cpp',[],objects=objects)
        offscreen=env.copy();offscreen['QT_QPA_PLATFORM']='offscreen';offscreen['QT_QPA_FONTDIR']=str(Path(os.environ['SystemRoot'])/'Fonts')
        for case in ['axis','data_export','buffer']:
            for scale in ([1,1.5] if case=='buffer' else [1]):
                label='desktop_'+case+'_'+str(scale);scratch=build/label;scratch.mkdir(exist_ok=False);child=offscreen.copy();child['QT_SCALE_FACTOR']=str(scale)
                capture(label,run(label+'_run',[exe,desktop_version_path(),case],scratch,child))
        exe=qt_build('mainwindow_probe','mainwindow_probe.cpp',[],objects=objects)
        for case in ['acceptance121','restart','empty_measure','semantics','waiting_resume','measurement','analysis_gates','exports','export_aliases','source_reconfigure']:
            scratch=build/('mainwindow_'+case);scratch.mkdir(exist_ok=False)
            capture('mainwindow_'+case,run('mainwindow_'+case+'_run',[exe,case,ROOT/'tools/tests/fixtures/TH3_acceptance.txt',desktop_version_path()],scratch,offscreen))
    out={'classification':'SOFTWARE FIXTURES / NOT TESTED ON HARDWARE','results':results,'pass':all(x['pass'] for x in results)}
    (raw/'regression_summary.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
    print(json.dumps({'pass':out['pass'],'suites':len(results),'summary':str(raw/'regression_summary.json')}),flush=True)
    return 0 if out['pass'] else 1
if __name__=='__main__':raise SystemExit(main())
