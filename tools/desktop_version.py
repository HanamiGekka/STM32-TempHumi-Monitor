"""Desktop override; root VERSION continues to own the MCU source identity."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
def desktop_version_path(root=ROOT):
    override=root/'UIDesktop/VERSION'
    return override if override.is_file() else root/'VERSION'
def desktop_version(root=ROOT):
    value=desktop_version_path(root).read_text(encoding='utf-8').strip()
    if not re.fullmatch(r'v\d+\.\d+\.\d+',value):raise ValueError('Invalid desktop version')
    return value
