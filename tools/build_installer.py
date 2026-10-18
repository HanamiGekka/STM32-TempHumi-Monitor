"""Wrap verified portable bytes in an unsigned, current-user Inno Setup installer."""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ACCEPTED_IDENTITY = ROOT / ('docs/experiments/EXP-20261017-001_recovery_identity_release/'
                            'raw/revalidation_20261017_1507/release_identity.json')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_bytes((json.dumps(value, indent=2) + '\n').encode('utf-8'))


def find_compiler():
    for drive in ('E', 'C', 'D'):
        for program_files in ('Program Files', 'Program Files (x86)'):
            for version in ('7',):
                candidate = Path(f'{drive}:/{program_files}/Inno Setup {version}/ISCC.exe')
                if candidate.is_file():
                    return candidate
    raise FileNotFoundError('Inno Setup 7 compiler not found; install an official builder on E:')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--desktop', type=Path)
    parser.add_argument('--identity', type=Path, default=ACCEPTED_IDENTITY)
    parser.add_argument('--portable-zip', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/release')
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--build-summary', type=Path,
                        help='Fresh desktop build receipt for a new public-source release')
    parser.add_argument('--firmware-build-summary', type=Path,
                        help='Paired fresh MCU receipt; unchanged firmware bytes can be reused')
    args = parser.parse_args()
    identity = json.loads(args.identity.read_text(encoding='utf-8-sig'))
    version = identity['version']
    desktop = (args.desktop or ROOT / 'dist/release' / version).resolve()
    portable_zip = (args.portable_zip or desktop.parent / f'TempHumi_{version}_win64.zip').resolve()
    assert digest(portable_zip) == identity['desktop_zip_sha256'], 'Unaccepted portable ZIP bytes'
    manifest = json.loads((desktop / 'release_manifest.json').read_text(encoding='utf-8-sig'))
    assert digest(desktop / 'TempHumi.exe') == identity['qt_exe_sha256'], 'Unaccepted EXE bytes'
    assert manifest['build_head'] == identity['source_commit']
    # Reuse broad QA only while every production input and paired release byte matches.
    baseline = args.identity.parent
    if args.build_summary or args.firmware_build_summary:
        assert args.build_summary and args.firmware_build_summary, 'Both fresh build receipts are required'
        summaries = [args.build_summary, args.firmware_build_summary]
    else:
        summaries = [baseline / name for name in
                     ('candidate_qt/candidate_summary.json', 'candidate_mcu/mcu_build_summary.json')]
    for summary_path in summaries:
        summary = json.loads(summary_path.read_text(encoding='utf-8-sig'))
        assert summary['pass'], 'Build receipt must have passed'
        for relative, expected in summary['source_hashes'].items():
            assert digest(ROOT / relative) == expected, relative
    compiler = (args.compiler or find_compiler()).resolve()
    assert compiler.is_file()
    compiler_version = subprocess.check_output([str(compiler), '--version'], text=True).strip()
    assert int(compiler_version.split('.')[0]) >= 7, 'This wrapper uses the Inno Setup 7 command-line interface'
    args.evidence.mkdir(parents=True, exist_ok=False)
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output / f'TempHumi_{version}_Setup.exe'
    assert not output.exists(), 'Preserve previous installers; use a new reviewed output directory'
    package_check = args.evidence / 'portable_check.json'
    check = subprocess.run([__import__('sys').executable, '-X', 'utf8', str(ROOT / 'tools/check_desktop_package.py'),
                            '--desktop', str(desktop), '--zip', str(portable_zip), '--output', str(package_check)],
                           cwd=ROOT, capture_output=True)
    (args.evidence / 'portable_check.log').write_bytes(check.stdout + check.stderr)
    assert check.returncode == 0, 'Portable package integrity failed'
    files = sorted(path for path in desktop.rglob('*') if path.is_file())
    assert all(not path.is_symlink() for path in files)
    # Enumerate exact verified files, rather than installing arbitrary future runtime output.
    entries = []
    payload = []
    for path in files:
        relative = path.relative_to(desktop)
        destination = '{app}' + ('\\' + str(relative.parent) if relative.parent != Path('.') else '')
        entries.append(f'Source: "{path}"; DestDir: "{destination}"; Flags: ignoreversion')
        payload.append({'relative':relative.as_posix(), 'bytes':path.stat().st_size, 'sha256':digest(path),
                        'origin':'Accepted portable payload'})
    license_path = compiler.parent / 'license.txt'
    assert license_path.is_file()
    entries.append(f'Source: "{license_path}"; DestDir: "{{app}}\\licenses\\installer"; '
                   'DestName: "Inno_Setup_LICENSE.txt"; Flags: ignoreversion')
    payload.append({'relative':'licenses/installer/Inno_Setup_LICENSE.txt', 'bytes':license_path.stat().st_size,
                    'sha256':digest(license_path), 'origin':str(license_path)})
    include = (args.evidence / 'installer_payload.iss').resolve()
    include.write_bytes(('\n'.join(entries) + '\n').encode('utf-8-sig'))
    script = ROOT / 'tools/installer/TempHumi.iss'
    numeric = '.'.join(str(int(part)) for part in version.lstrip('v').split('.')) + '.0'
    command = [str(compiler), '--no-ide-signtools', '--quiet',
               '--define=ProductVersion=' + version, '--define=NumericVersion=' + numeric,
               '--define=ProjectRoot=' + str(ROOT), '--define=OutputDirectory=' + str(args.output.resolve()),
               '--define=PayloadInclude=' + str(include), str(script)]
    build = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=180)
    (args.evidence / 'compile.log').write_bytes(build.stdout + build.stderr)
    assert build.returncode == 0 and output.is_file(), (build.stdout + build.stderr).decode(errors='replace')
    assert digest(desktop / 'TempHumi.exe') == identity['qt_exe_sha256']
    receipt = {'at':datetime.now(timezone.utc).isoformat(), 'pass':True, 'version':version,
               'production_commit':identity['source_commit'], 'build_checkout_head':subprocess.check_output(
                   ['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
               'portable_exe_sha256':identity['qt_exe_sha256'], 'portable_files':len(files),
               'compiler':str(compiler), 'compiler_version':compiler_version, 'compiler_sha256':digest(compiler),
               'script_sha256':digest(script), 'setup_path':str(output.resolve()), 'setup_bytes':output.stat().st_size,
               'setup_sha256':digest(output), 'signature':'UNSIGNED; no certificate configured',
               'payload':payload, 'default_path':'E:/Program Files/TempHumi Monitor when available; otherwise current-user Programs',
               'scope':'Packaging only. No Qt/MCU rebuild, COM connection, firmware write, user-config deletion or data migration.'}
    save(args.evidence / 'installer_identity.json', receipt)
    print(json.dumps({key:receipt[key] for key in ('pass','version','compiler_version','setup_path','setup_bytes','setup_sha256')}))


if __name__ == '__main__':
    main()
