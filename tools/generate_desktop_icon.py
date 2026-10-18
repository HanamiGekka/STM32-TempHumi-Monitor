"""Rebuild the project's own yellow/white TH icon; no third-party artwork."""
from pathlib import Path
from desktop_version import desktop_version
import re
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]; OUT=ROOT/'UIDesktop/resources'; OUT.mkdir(exist_ok=True)
image=Image.new('RGBA',(512,512),(0,0,0,0)); drawing=ImageDraw.Draw(image)
drawing.rounded_rectangle((12,12,500,500),radius=62,fill='#C99B05')
font=ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf',235)
box=drawing.textbbox((0,0),'TH',font=font)
drawing.text(((512-(box[2]-box[0]))/2-box[0],(512-(box[3]-box[1]))/2-box[1]),'TH',font=font,fill='white')
image.save(OUT/'temphumi.png')
image.save(OUT/'temphumi.ico',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
(OUT/'temphumi.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512"><rect x="12" y="12" width="488" height="488" rx="62" fill="#C99B05"/><text x="256" y="338" text-anchor="middle" font-family="Arial,sans-serif" font-weight="bold" font-size="235" fill="white">TH</text></svg>\n',encoding='utf-8')
version=desktop_version(ROOT)
match=re.fullmatch(r'v(\d+)\.(\d+)\.(\d+)',version)
if not match: raise RuntimeError('VERSION must use v<major>.<minor>.<patch>')
numbers=[int(x) for x in match.groups()]+[0]
if any(x>65535 for x in numbers): raise RuntimeError('Windows numeric version overflow')
template=(OUT/'temphumi.rc.in').read_text(encoding='utf-8')
rc=template.replace('@NUMERIC_VERSION@',','.join(map(str,numbers))).replace('@FILE_VERSION@','.'.join(map(str,numbers))).replace('@PRODUCT_VERSION@',version)
(OUT/'temphumi.rc').write_text('// Generated from desktop VERSION override (root fallback); edit the template/generator, not this file.\n'+rc,encoding='utf-8')
print(str(OUT/'temphumi.ico'))
