"""Update desktop PE metadata without changing firmware source/version/artwork."""
from pathlib import Path
import re
from desktop_version import desktop_version
ROOT=Path(__file__).resolve().parents[1]
def update_version_resource():
    version=desktop_version()
    match=re.fullmatch(r'v(\d+)\.(\d+)\.(\d+)',version)
    if not match:raise ValueError('Invalid canonical VERSION')
    values=[int(x) for x in match.groups()]+[0]
    if any(x>65535 for x in values):raise ValueError('Windows version overflow')
    path=ROOT/'UIDesktop/resources/temphumi.rc'
    template=path.with_suffix('.rc.in').read_text(encoding='utf-8')
    content='// Generated from desktop VERSION (root VERSION fallback); edit the template/generator.\n'+template.replace('@NUMERIC_VERSION@',','.join(map(str,values))).replace('@FILE_VERSION@','.'.join(map(str,values))).replace('@PRODUCT_VERSION@',version)
    if not path.exists() or path.read_bytes()!=content.encode('utf-8'):path.write_bytes(content.encode('utf-8'))
if __name__=='__main__':update_version_resource()
