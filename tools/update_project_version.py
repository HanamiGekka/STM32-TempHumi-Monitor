"""Generate the MCU alias from root VERSION; desktop may have its own override."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
def update_project_version():
    version=(ROOT/'VERSION').read_text(encoding='utf-8').strip()
    if not re.fullmatch(r'v\d+\.\d+\.\d+',version):raise ValueError('Invalid canonical project VERSION')
    p=ROOT/'Core/Inc/app_project_version.h'
    content=('/* Generated from root VERSION by tools/update_project_version.py. */\n'
             '#ifndef APP_PROJECT_VERSION_H\n#define APP_PROJECT_VERSION_H\n'
             f'#define APP_PROJECT_VERSION_STR "{version}"\n#endif\n')
    if not p.exists() or p.read_bytes()!=content.encode():p.write_bytes(content.encode())
if __name__=='__main__':update_project_version()
