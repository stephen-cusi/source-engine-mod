import os, shutil, re
std = r'D:\project\source-engine\materialsystem\stdshaders'
ws = r'D:\tmp\ws_teeth'
FILES = ['teeth_vs20.fxc', 'teeth_bump_vs20.fxc', 'teeth_flashlight_vs20.fxc']
HEADERS = ['vortwarp_vs20_helper.h', 'common_vs_fxc.h', 'common_fxc.h',
           'common_pragmas.h', 'common_hlsl_cpp_consts.h']
from patch_fxc import BLOCK, ANCHOR
for variant in ('src_orig', 'src_patched'):
    d = os.path.join(ws, variant)
    os.makedirs(d, exist_ok=True)
    for h in HEADERS:
        shutil.copyfile(os.path.join(std, h), os.path.join(d, h))
for f in FILES:
    cur = open(os.path.join(std, f), 'rb').read()
    assert BLOCK in cur, f
    orig = cur.replace(BLOCK, b'', 1)
    assert BLOCK not in orig
    open(os.path.join(ws, 'src_orig', f), 'wb').write(orig)
    open(os.path.join(ws, 'src_patched', f), 'wb').write(cur)
    print('%-26s orig=%d patched=%d' % (f, len(orig), len(cur)))
# prove src_orig == the pre-patch file (144 bytes smaller, same CRLF/high bytes)
for f in FILES:
    d = open(os.path.join(ws, 'src_orig', f), 'rb').read()
    print('%-26s CRLF=%d loneLF=%d high=%r' % (
        f, d.count(b'\r\n'), d.count(b'\n') - d.count(b'\r\n'),
        sorted({b for b in d if b > 0x7f})))
