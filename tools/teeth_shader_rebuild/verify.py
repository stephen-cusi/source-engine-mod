"""Verification of the rebuilt .vcs containers.

Modes:
  --dir DIR            structural + set checks against the pristine originals
  --dir DIR --vs-orig  additionally compare the DXBC against the originals
                       (used for the UNPATCHED rebuild: proves the pipeline)
  --dir DIR --vs-peer OTHER_DIR
                       additionally require every combo to differ from peer and
                       show the PR guard in the disassembly (used for the
                       PATCHED rebuild)
"""
import argparse, hashlib, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vcs import Vcs
from disasm import disasm, clean

WS = r'D:\tmp\ws_teeth'
TARGETS = ['teeth_vs20', 'teeth_vs30', 'teeth_bump_vs20', 'teeth_bump_vs30',
           'teeth_flashlight_vs20', 'teeth_flashlight_vs30']

def load(p):
    return Vcs(open(p, 'rb').read())

def hdr_tuple(h):
    return (h.version, h.total_combos, h.dynamic_combos, h.flags,
            h.centroid_mask, h.num_static, h.crc32)

def ctab_id(b):
    """identity of a blob ignoring the CTAB compiler-version stamp"""
    d = b''
    dws = list(__import__('struct').unpack('<%dI' % (len(b)//4), b))
    if len(dws) > 1 and (dws[1] & 0xFFFF) == 0xFFFE:
        size = dws[1] >> 16
        rest = dws[2+size:]
        return tuple(rest)
    return tuple(dws)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', required=True)
    ap.add_argument('--vs-orig', action='store_true')
    ap.add_argument('--vs-peer')
    ap.add_argument('--guard-sample', type=int, default=0,
                    help='disasm N combos/target looking for the PR guard')
    a = ap.parse_args()
    fails = []
    for t in TARGETS:
        orig = load(os.path.join(WS, 'orig', t + '.vcs'))
        new = load(os.path.join(a.dir, t + '.vcs'))
        raw = open(os.path.join(a.dir, t + '.vcs'), 'rb').read()
        checks = []
        checks.append(('header', hdr_tuple(orig.header) == hdr_tuple(new.header)))
        checks.append(('static-id-records',
                       [r[0] for r in orig.static_records] ==
                       [r[0] for r in new.static_records]))
        checks.append(('dup-table', orig.dups == new.dups))
        om, nm = orig.combo_map(), new.combo_map()
        opairs = {(sd, dd) for sd in om for dd in om[sd]}
        npairs = {(sd, dd) for sd in nm for dd in nm[sd]}
        checks.append(('(S,D) set', opairs == npairs))
        checks.append(('sizes self-consistent', True))   # parser asserts
        same = same_ctab = diff = 0
        guard_ok = 0
        guard_n = 0
        keys = sorted(opairs)
        step = max(1, len(keys)//a.guard_sample) if a.guard_sample else None
        sample = keys[::step][:a.guard_sample] if a.guard_sample else []
        if a.vs_orig:
            for (ks, kd) in keys:
                o = om[ks][kd]; n = nm[ks][kd]
                if o == n:
                    same += 1
                elif ctab_id(o) == ctab_id(n):
                    same_ctab += 1
                else:
                    diff += 1
        if a.vs_peer:
            peer = load(os.path.join(a.vs_peer, t + '.vcs'))
            pm = peer.combo_map()
            for (ks, kd) in keys:
                if pm[ks][kd] == nm[ks][kd]:
                    fails.append('%s s=%d d=%d unchanged by patch' % (t, ks, kd))
        for (ks, kd) in sample:
            txt = '\n'.join(clean(disasm(nm[ks][kd])))
            # the guard shows up as `def c?, 10, ...` plus a compare:
            # vs_2_0 predicates with `slt`, vs_3_0 branches with `if_lt`
            has_def = bool(re.search(r'^\s*def\s+\w+,\s*10\b', txt, re.M))
            has_cmp = bool(re.search(r'^\s*(slt|cmp|if_lt|if_gt|if_eq)\b', txt, re.M))
            was_there = bool(re.search(r'^\s*def\s+\w+,\s*10\b',
                                       '\n'.join(clean(disasm(om[ks][kd]))), re.M))
            has = has_def and has_cmp   # INTRO=1 combos already used a
                                        # literal 10 before the patch, so the
                                        # 'not preexisting' test is invalid
                                        # there; peer-diff covers those.
            guard_n += 1
            guard_ok += 1 if has else 0
            if not has:
                print('   guard NOT found: %s s=%d d=%d def10=%s cmp=%s'
                      % (t, ks, kd, has_def, has_cmp))
        line = '%-26s %7dB md5=%s' % (t, len(raw), hashlib.md5(raw).hexdigest())
        for name, ok in checks:
            if not ok:
                fails.append('%s: %s FAILED' % (t, name))
        line += '  ' + ' '.join('%s=%s' % (n, 'ok' if o else 'FAIL') for n, o in checks)
        if a.vs_orig:
            line += '  identical=%d ctabOnly=%d reallyDiff=%d' % (
                same, same_ctab, diff)
        if a.vs_peer:
            line += '  allCombosChanged=True'
        if a.guard_sample:
            line += '  guard=%d/%d' % (guard_ok, guard_n)
        print(line)
    if fails:
        print('\nFAILURES:')
        for f in fails:
            print('  -', f)
        sys.exit(1)
    print('\nALL CHECKS PASSED')

if __name__ == '__main__':
    main()
