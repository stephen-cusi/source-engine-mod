"""Apply PR ValveSoftware/source-sdk-2013#1382 to the fork's teeth VS .fxc files.

The PR's 6 lines use an early `return o;`.  The in-tree compiler
(dx9sdk/utilities/fxc.exe, D3DX9 5.04.00.2904) rejects that with
`X3500: asymetric returns from if statements not yet implemented` - and it
rejects the same return inside a plain if/else too.  It is the only compiler
available that can build these files as vs_3_0 (the modern Windows-SDK fxc
refuses `D3DVERTEXTEXTURESAMPLER0`, X3089), so the guard is expressed with a
single trailing return instead:

    if(length(v.vPos.xy) > 10)
    {
        float4 FinalProj = mul( float4(0.0, 0.0, 0.0, 1.0), cViewProj );
        o.projPos = FinalProj;
    }
    else
    {
        ... original body, untouched ...
    }
    return o;

Semantics are identical to the PR: `o` is zero-initialised, so a garbage
vertex still ends up with only projPos written while every other output stays
zero; a good vertex runs the original body.

Files are latin1 (a (c) byte) and CRLF, so all IO is binary: rb/wb + bytes
splices only.
"""
import os
import shutil

ANCHOR = b'VS_OUTPUT o = ( VS_OUTPUT )0;'
PR_BLOCK = (                      # the PR's literal 6 lines
    b'\tif(length(v.vPos.xy) > 10)\r\n'
    b'\t{\r\n'
    b'\t\tfloat4 FinalProj = mul( float4(0.0, 0.0, 0.0, 1.0), cViewProj );\r\n'
    b'\t\to.projPos = FinalProj;\r\n'
    b'\t\treturn o;\r\n'
    b'\t}\r\n'
)
GUARD_TOP = (                      # same, minus the early return, plus `else {`
    b'\tif(length(v.vPos.xy) > 10)\r\n'
    b'\t{\r\n'
    b'\t\tfloat4 FinalProj = mul( float4(0.0, 0.0, 0.0, 1.0), cViewProj );\r\n'
    b'\t\to.projPos = FinalProj;\r\n'
    b'\t}\r\n'
    b'\telse\r\n'
    b'\t{\r\n'
)
TAIL = b'\treturn o;\r\n}\r\n'      # main()'s single trailing return
TAIL_NEW = b'\t}\r\n\treturn o;\r\n}\r\n'

SRC = r'D:\project\source-engine\materialsystem\stdshaders'
FILES = ['teeth_vs20.fxc', 'teeth_bump_vs20.fxc', 'teeth_flashlight_vs20.fxc']


def patch_bytes(data, path):
    if GUARD_TOP in data:
        return data, 'already patched'
    if PR_BLOCK in data:                     # undo a verbatim-PR attempt
        data = data.replace(PR_BLOCK, b'', 1)
        assert PR_BLOCK not in data
    i = data.find(ANCHOR)
    assert i != -1, 'anchor missing in %s' % path
    assert data.find(ANCHOR, i + 1) == -1, 'anchor not unique in %s' % path
    eol = data.index(b'\r\n', i) + 2
    assert data[eol:eol + 2] == b'\r\n', 'expected blank line after anchor'
    assert data.count(TAIL) == 1, 'main tail not unique in %s' % path
    out = data[:eol] + GUARD_TOP + data[eol:]
    out = out.replace(TAIL, TAIL_NEW, 1)
    # invariants: CRLF only, high bytes untouched, exactly one new top-level `}`
    assert out.count(b'\n') == out.count(b'\r\n'), 'lone LF introduced'
    hi_old = sorted({b for b in data if b > 0x7F})
    hi_new = sorted({b for b in out if b > 0x7F})
    assert hi_old == hi_new and hi_old, (hi_old, hi_new)
    return out, 'patched'


def main():
    for f in FILES:
        path = os.path.join(SRC, f)
        data = open(path, 'rb').read()
        new, state = patch_bytes(data, path)
        if state == 'patched':
            open(path, 'wb').write(new)
        print('%-26s %-16s %d -> %d bytes (+%d)' % (
            f, state, len(data), len(new), len(new) - len(data)))


if __name__ == '__main__':
    main()
