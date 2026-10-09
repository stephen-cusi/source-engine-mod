"""Offline rebuild of the teeth_* vertex shader .vcs containers.

Pipeline mirrors Valve's:
  devtools/bin/fxc_prep.pl        - parse // STATIC:/DYNAMIC:/SKIP: headers,
                                    honour the [vs20]/[vs30] line tags
  utils/shadercompile/cfgprocessor.cpp - combo id <-> macro value encoding
  utils/shadercompile/shadercompile.cpp - .vcs assembly
  fxc.exe (dx9sdk/utilities)      - DXBC compilation

Stages:
  probe  : compile a few combos from the *unpatched* source and byte-compare
           them against the DXBC stored in the original .vcs
  build  : compile every (static,dynamic) pair the original .vcs contains and
           reassemble the container
"""
import hashlib
import os
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vcs import Vcs, build, SENTINEL

WS = r'D:\tmp\ws_teeth'
SRCDIR = r'D:\project\source-engine\materialsystem\stdshaders'
FXC = r'D:\project\source-engine\dx9sdk\utilities\fxc.exe'
ORIG = os.path.join(WS, 'orig')

# target .vcs base name -> (source .fxc, output profile suffix)
# buildallshaders.bat:
#   buildshaders stdshader_dx9_20b                    -> *_vs20.vcs, vs_2_0
#   buildshaders stdshader_dx9_30 -dx9_30 -force30    -> *_vs30.vcs, vs_3_0
# fxc_prep.pl GetShaderType() picks the profile from the *output* basename,
# LoadShaderListFile() rewrites _vs20 -> _vs30 under DIRECTX_FORCE_MODEL=30.
TARGETS = {
    'teeth_vs20':            ('teeth_vs20.fxc', 'vs_2_0'),
    'teeth_vs30':            ('teeth_vs20.fxc', 'vs_3_0'),
    'teeth_bump_vs20':       ('teeth_bump_vs20.fxc', 'vs_2_0'),
    'teeth_bump_vs30':       ('teeth_bump_vs20.fxc', 'vs_3_0'),
    'teeth_flashlight_vs20': ('teeth_flashlight_vs20.fxc', 'vs_2_0'),
    'teeth_flashlight_vs30': ('teeth_flashlight_vs20.fxc', 'vs_3_0'),
}


class Define:
    def __init__(self, name, lo, hi, static):
        self.name, self.lo, self.hi, self.static = name, lo, hi, static

    @property
    def rng(self):
        return self.hi - self.lo + 1


def parse_fxc(src_path, target_basename):
    """Faithful port of the relevant fxc_prep.pl header parsing."""
    raw = open(src_path, 'rb').read().decode('latin1')     # latin1: file has (c)
    lines = raw.split('\n')

    vsver = None
    m = re.search(r'_vs(\d+\w?)$', target_basename, re.I)
    if m:
        vsver = m.group(1)

    out_lines = []
    for line in lines:
        if vsver is not None and re.search(r'\[vs\d+\w?\]', line, re.I):
            if not re.search(r'\[vs%s\]' % re.escape(vsver), line, re.I):
                line = ''
        # fxc_prep strips every [...] chunk from the line
        line = re.sub(r'\[[^\[\]]*\]', '', line)
        out_lines.append(line)

    statics, dynamics, skips, centroids = [], [], [], []
    for line in out_lines:
        if not line.strip():
            continue
        m = re.match(r'^\s*//\s*STATIC\s*\:\s*\"(.*)\"\s+\"(\d+)\.\.(\d+)\"', line)
        if m:
            statics.append(Define(m.group(1), int(m.group(2)), int(m.group(3)), True))
            continue
        m = re.match(r'^\s*//\s*DYNAMIC\s*\:\s*\"(.*)\"\s+\"(\d+)\.\.(\d+)\"', line)
        if m:
            dynamics.append(Define(m.group(1), int(m.group(2)), int(m.group(3)), False))
            continue
    for line in out_lines:
        m = re.match(r'^\s*//\s*SKIP\s*\s*\:\s*(.*)$', line)
        if m:
            skips.append(m.group(1).strip())
    for line in out_lines:
        m = re.match(r'^\s*//\s*CENTROID\s*\:\s*TEXCOORD(\d+)\s*$', line)
        if m:
            centroids.append(int(m.group(1)))
    return statics, dynamics, skips, centroids


def product(seq):
    r = 1
    for x in seq:
        r *= x
    return r


def decode(idx, defs):
    """cfgprocessor combo id -> {name: value}; first define is least
    significant (ComboGenerator::RunAllCombos decrements the first slot
    fastest), and shadercompile splits with
    static = combo / numDynamicCombos."""
    vals = {}
    for d in defs:
        vals[d.name] = d.lo + idx % d.rng
        idx //= d.rng
    assert idx == 0, (idx, defs)
    return vals


def encode(vals, defs):
    idx = 0
    stride = 1
    for d in defs:
        idx += (vals[d.name] - d.lo) * stride
        stride *= d.rng
    return idx


def eval_skip(skips, vals):
    """fxc_prep concatenates skips as ( expr )|| ... ||0."""
    for expr in skips:
        e = re.sub(r'\$(\w+)', lambda m: '(%d)' % vals[m.group(1)], expr)
        e = e.replace('&&', ' and ').replace('||', ' or ')
        e = re.sub(r'!(?!=)', ' not ', e)
        if eval(e):
            return True
    return False


def combo_macros(statics, dynamics, sid, did):
    vals = decode(did, dynamics)
    vals.update(decode(sid, statics))
    return vals


def fxc_command(src, target, profile, statics, dynamics, vals, combo_id, fo_path):
    total = product([d.rng for d in statics]) * product([d.rng for d in dynamics])
    ndyn = product([d.rng for d in dynamics])
    centroid = 0
    parts = [FXC,
             '/DTOTALSHADERCOMBOS=%d' % total,
             '/DCENTROIDMASK=%d' % centroid,
             '/DNUMDYNAMICCOMBOS=%d' % ndyn,
             '/DFLAGS=0x0',
             '/DSHADERCOMBO=%d' % combo_id]
    for d in dynamics + statics:            # cfgprocessor emits in add order
        parts.append('/D%s=%d' % (d.name, vals[d.name]))
    parts += ['/Dmain=main', '/Emain', '/T%s' % profile,
              '/DSHADER_MODEL_%s=1' % profile.upper(),
              '/nologo', '/Fo%s' % fo_path, src]
    return parts


def run_fxc(cmd, log):
    r = subprocess.run(cmd, cwd=WS, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    out = r.stdout.decode('latin1', 'replace')
    return r.returncode, out


def load_target(target, patched_dir=None):
    """Returns (statics, dynamics, skips, centroids, present set, Vcs)."""
    src_base, profile = TARGETS[target]
    src_dir = patched_dir or SRCDIR
    statics, dynamics, skips, centroids = parse_fxc(
        os.path.join(src_dir, src_base), target)
    vc = Vcs(open(os.path.join(ORIG, target + '.vcs'), 'rb').read())
    present = set()
    for sid, entries in vc.sections.items():
        for e in entries:
            if e[0] == 'combo':
                present.add((sid, e[1]))
    return statics, dynamics, skips, centroids, present, vc, src_base, profile


def validate_parsing(target):
    statics, dynamics, skips, centroids, present, vc, src_base, profile = \
        load_target(target)
    h = vc.header
    n_static = product([d.rng for d in statics])
    n_dyn = product([d.rng for d in dynamics])
    assert h.total_combos == n_static * n_dyn, (h.total_combos, n_static, n_dyn)
    assert h.dynamic_combos == n_dyn, (h.dynamic_combos, n_dyn)
    expected = set()
    for sid in range(n_static):
        for did in range(n_dyn):
            vals = combo_macros(statics, dynamics, sid, did)
            if not eval_skip(skips, vals):
                expected.add((sid, did))
    ok = (expected == present)
    print('%-26s statics=%s dynamics=%s skip=%s centroid=%d' % (
        target, [(d.name, d.lo, d.hi) for d in statics],
        [(d.name, d.lo, d.hi) for d in dynamics], skips,
        sum(1 << c for c in centroids)))
    print('   header total=%d dyn=%d | parsed %d static x %d dyn = %d | present=%d expected=%d match=%s'
          % (h.total_combos, h.dynamic_combos, n_static, n_dyn, n_static * n_dyn,
             len(present), len(expected), ok))
    assert ok, 'combo/skip model does not reproduce the original .vcs content'
    return (statics, dynamics, skips, centroids, present, vc, src_base, profile)


def compile_combo(target, sid, did, statics, dynamics, src_base, profile,
                  patched_dir, out_o, log):
    src_dir = patched_dir or SRCDIR
    src = os.path.join(src_dir, src_base)
    vals = combo_macros(statics, dynamics, sid, did)
    total = product([d.rng for d in statics]) * product([d.rng for d in dynamics])
    combo_id = encode(vals, dynamics) + encode(vals, statics) * product(
        [d.rng for d in dynamics])
    cmd = fxc_command(src, target, profile, statics, dynamics, vals,
                      combo_id, out_o)
    rc, out = run_fxc(cmd, log)
    if rc != 0 or not os.path.exists(out_o) or os.path.getsize(out_o) == 0:
        log.write('FAIL %s s=%d d=%d rc=%d\n%s\n' % (target, sid, did, rc, out))
        return None
    if 'error' in out.lower():
        log.write('WARN %s s=%d d=%d\n%s\n' % (target, sid, did, out))
    data = open(out_o, 'rb').read()
    return data


def stage_probe(targets, n=6):
    log = open(os.path.join(WS, 'probe.log'), 'w')
    total_ok = total = 0
    for t in targets:
        statics, dynamics, skips, centroids, present, vc, src_base, profile = \
            validate_parsing(t)
        pairs = sorted(present)
        step = max(1, len(pairs) // n)
        sample = pairs[::step][:n]
        print('  probe %s: %d/%d combos' % (t, len(sample), len(pairs)))
        for sid, did in sample:
            total += 1
            orig = vc.dxbc(sid, did)
            got = compile_combo(t, sid, did, statics, dynamics, src_base,
                                profile, None,
                                os.path.join(WS, 'probe_%s_%d_%d.o' % (t, sid, did)),
                                log)
            if got is None:
                print('    s=%d d=%d COMPILE FAIL (see probe.log)' % (sid, did))
                continue
            if got == orig:
                total_ok += 1
                print('    s=%d d=%d IDENTICAL (%d bytes)' % (sid, did, len(got)))
            else:
                print('    s=%d d=%d differs orig=%d new=%d md5 %s vs %s' % (
                    sid, did, len(orig), len(got),
                    hashlib.md5(orig).hexdigest()[:8],
                    hashlib.md5(got).hexdigest()[:8]))
    print('PROBE: %d/%d byte-identical' % (total_ok, total))
    log.close()
    return total_ok, total


def stage_build(target, patched_dir, out_path):
    statics, dynamics, skips, centroids, present, vc, src_base, profile = \
        load_target(target, patched_dir)
    h = vc.header
    log = open(os.path.join(WS, 'build_%s.log' % target), 'w')
    combos = {}
    tdir = os.path.join(WS, 'obj_%s' % os.path.basename(out_path))
    os.makedirs(tdir, exist_ok=True)
    for sid, did in sorted(present):
        data = compile_combo(target, sid, did, statics, dynamics, src_base,
                             profile, patched_dir,
                             os.path.join(tdir, '%d_%d.o' % (sid, did)), log)
        if data is None:
            raise SystemExit('compile failed for %s s=%d d=%d' % (target, sid, did))
        combos[(sid, did)] = data
    log.close()
    # reassemble preserving header / record ids / dup table
    sec = []
    for sid, _off in vc.static_records:
        if sid not in vc.sections:
            continue
        entries = [('combo', d, combos[(sid, d)])
                   for d in sorted(k[1] for k in combos if k[0] == sid)]
        sec.append((sid, entries))
    out = build(vc.header, vc.static_records, vc.dups, sec)
    open(out_path, 'wb').write(out)
    return out, combos


if __name__ == '__main__':
    mode = sys.argv[1]
    if mode == 'probe':
        stage_probe(sys.argv[2:] or list(TARGETS))
    elif mode == 'validate':
        for t in (sys.argv[2:] or list(TARGETS)):
            validate_parsing(t)
    elif mode == 'build':
        patched = sys.argv[2] or None
        outdir = sys.argv[3]
        os.makedirs(outdir, exist_ok=True)
        for t in TARGETS:
            out, combos = stage_build(t, patched, os.path.join(outdir, t + '.vcs'))
            print('%-26s -> %d bytes, %d combos, md5=%s' % (
                t, len(out), len(combos), hashlib.md5(out).hexdigest()))
