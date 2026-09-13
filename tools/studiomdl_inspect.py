#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
studiomdl_inspect.py - offline .mdl inspector for StudioMDL (Source) model files.

Read-only. Parses the binary .mdl directly using the struct layouts declared in
public/studio.h of this engine fork (STUDIO_VERSION 49 structs; the engine also
loads legacy v44..v48 files with the same layouts via
Studio_ConvertStudioHdrToNewVersion()).

Motivation (HL2SB / Garry's Mod player animation port):
  Garry's Mod's player animation libraries (models/m_anm.mdl, f_anm.mdl, z_anm.mdl)
  store sequence *activity numbers* computed against GMod's closed-source activity
  enum, which does NOT match this engine's enum.  Therefore sequences must be
  selected BY ACTIVITY NAME (szactivitynameindex -> "ACT_*"), not by number.
  This tool dumps the authoritative name<->number mapping plus the pose parameters
  and IK chains so the design can be driven by names.

Usage:
  python studiomdl_inspect.py <model1.mdl> [<model2.mdl> ...]
  python studiomdl_inspect.py --json out.json <model.mdl>
  python studiomdl_inspect.py --all-activities <model.mdl>

Exit code 0 on success, 1 if any file failed self-validation.
"""

import argparse
import json
import os
import struct
import sys

# ---------------------------------------------------------------------------
# Struct layouts (byte offsets), derived from public/studio.h
# ---------------------------------------------------------------------------

# studiohdr_t -- sizeof == 408 for version 44..49 files that carry studiohdr2_t
STUDIOHDR = {
    "id": 0,
    "version": 4,
    "checksum": 8,
    "name": 12,          # char[64]
    "length": 76,
    "eyeposition": 80,
    "illumposition": 92,
    "hull_min": 104,
    "hull_max": 116,
    "view_bbmin": 128,
    "view_bbmax": 140,
    "flags": 152,
    "numbones": 156,
    "boneindex": 160,
    "numbonecontrollers": 164,
    "bonecontrollerindex": 168,
    "numhitboxsets": 172,
    "hitboxsetindex": 176,
    "numlocalanim": 180,
    "localanimindex": 184,
    "numlocalseq": 188,
    "localseqindex": 192,
    "activitylistversion": 196,
    "eventsindexed": 200,
    "numtextures": 204,
    "textureindex": 208,
    "numcdtextures": 212,
    "cdtextureindex": 216,
    "numskinref": 220,
    "numskinfamilies": 224,
    "skinindex": 228,
    "numbodyparts": 232,
    "bodypartindex": 236,
    "numlocalattachments": 240,
    "localattachmentindex": 244,
    "numlocalnodes": 248,
    "localnodeindex": 252,
    "localnodenameindex": 256,
    "numflexdesc": 260,
    "flexdescindex": 264,
    "numflexcontrollers": 268,
    "flexcontrollerindex": 272,
    "numflexrules": 276,
    "flexruleindex": 280,
    "numikchains": 284,
    "ikchainindex": 288,
    "nummouths": 292,
    "mouthindex": 296,
    "numlocalposeparameters": 300,
    "localposeparamindex": 304,
    "surfacepropindex": 308,
    "keyvalueindex": 312,
    "keyvaluesize": 316,
    "numlocalikautoplaylocks": 320,
    "localikautoplaylockindex": 324,
    "mass": 328,
    "contents": 332,
    "numincludemodels": 336,
    "includemodelindex": 340,
    "unused_virtualModel": 344,
    "szanimblocknameindex": 348,
    "numanimblocks": 352,
    "animblockindex": 356,
    "unused_animblockModel": 360,
    "bonetablebynameindex": 364,
    "unused_pVertexBase": 368,
    "unused_pIndexBase": 372,
    "constdirectionallightdot": 376,
    "rootLOD": 377,
    "numAllowedRootLODs": 378,
    "unused_byte": 379,
    "unused4": 380,
    "numflexcontrollerui": 384,
    "flexcontrolleruiindex": 388,
    "flVertAnimFixedPointScale": 392,
    "unused3": 396,
    "studiohdr2index": 400,
    "unused2": 404,
}
STUDIOHDR_SIZE = 408

SEQDESC_SIZE = 212  # v44..v49


def u32(buf, off):
    return struct.unpack_from("<i", buf, off)[0]


def f32(buf, off):
    return struct.unpack_from("<f", buf, off)[0]


class MdlError(Exception):
    pass


class Mdl(object):
    """A parsed .mdl file. All offsets are relative to the start of the buffer."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            self.buf = fh.read()
        self.size = len(self.buf)
        self.warnings = []
        if self.size < STUDIOHDR_SIZE:
            raise MdlError("file too small (%d bytes)" % self.size)
        # magic: 'IDST' stored as 0x54534449
        magic = self.buf[0:4]
        if magic != b"IDST":
            raise MdlError("bad magic %r (expected b'IDST')" % (magic,))
        self.id = struct.unpack_from("<i", self.buf, 0)[0]
        self.version = u32(self.buf, STUDIOHDR["version"])
        self.checksum = u32(self.buf, STUDIOHDR["checksum"])
        raw_name = self.buf[STUDIOHDR["name"]:STUDIOHDR["name"] + 64]
        self.name = raw_name.split(b"\x00")[0].decode("ascii", "replace")
        self.length = u32(self.buf, STUDIOHDR["length"])
        self.flags = u32(self.buf, STUDIOHDR["flags"])
        self.studiohdr2index = u32(self.buf, STUDIOHDR["studiohdr2index"])
        self.hdr = self._h()
        self._validate_header()

    # -- helpers ------------------------------------------------------------

    def _h(self):
        """All header fields as a dict of ints (byte fields kept as ints)."""
        out = {}
        for k, off in STUDIOHDR.items():
            out[k] = u32(self.buf, off)
        out["name"] = self.name
        out["constdirectionallightdot"] = self.buf[STUDIOHDR["constdirectionallightdot"]]
        out["rootLOD"] = self.buf[STUDIOHDR["rootLOD"]]
        out["numAllowedRootLODs"] = self.buf[STUDIOHDR["numAllowedRootLODs"]]
        return out

    def cstr(self, off, limit=512):
        """Read a NUL-terminated ASCII string at absolute offset."""
        if off < 0 or off >= self.size:
            raise MdlError("string offset out of range: %d (file size %d)" % (off, self.size))
        end = self.buf.find(b"\x00", off, min(off + limit, self.size))
        if end < 0:
            end = min(off + limit, self.size)
        return self.buf[off:end].decode("ascii", "replace")

    @staticmethod
    def printable(s):
        return all(32 <= ord(c) < 127 for c in s)

    def _validate_header(self):
        if self.length != self.size:
            self.warnings.append(
                "header length (%d) != file size (%d)" % (self.length, self.size))
        if not (44 <= self.version <= 49):
            self.warnings.append("unexpected version %d" % self.version)
        for key in ("localseqindex", "localposeparamindex", "ikchainindex",
                    "boneindex", "includemodelindex", "localikautoplaylockindex"):
            off = self.hdr[key]
            if off < 0 or off > self.size:
                self.warnings.append("%s out of range (%d)" % (key, off))
        if not self.printable(self.name):
            self.warnings.append("model name is not printable: %r" % self.name)

    # -- header fields ------------------------------------------------------

    @property
    def numlocalseq(self):
        return self.hdr["numlocalseq"]

    @property
    def numbones(self):
        return self.hdr["numbones"]

    # -- bones --------------------------------------------------------------

    def bones(self):
        base = self.hdr["boneindex"]
        out = []
        for i in range(self.hdr["numbones"]):
            off = base + i * 216
            szname = u32(self.buf, off)
            parent = u32(self.buf, off + 4)
            out.append({
                "index": i,
                "name": self.cstr(off + szname),
                "parent": parent,
            })
        return out

    def bone_name(self, idx, bones=None):
        bones = bones if bones is not None else self.bones()
        if 0 <= idx < len(bones):
            return bones[idx]["name"]
        return "<bone %d out of range>" % idx

    # -- sequences ----------------------------------------------------------

    def sequences(self):
        base = self.hdr["localseqindex"]
        out = []
        for i in range(self.numlocalseq):
            off = base + i * SEQDESC_SIZE
            if off + SEQDESC_SIZE > self.size:
                self.warnings.append("sequence %d extends past EOF" % i)
                break
            szlabel = u32(self.buf, off + 4)
            szact = u32(self.buf, off + 8)
            label = self.cstr(off + szlabel) if szlabel else ""
            activity_name = self.cstr(off + szact) if szact else ""
            out.append({
                "index": i,
                "label": label,
                "activity_name": activity_name,
                "activity": u32(self.buf, off + 16),
                "flags": u32(self.buf, off + 12),
                "actweight": u32(self.buf, off + 20),
                "numblends": u32(self.buf, off + 56),
                "groupsize": [u32(self.buf, off + 68), u32(self.buf, off + 72)],
                "paramindex": [u32(self.buf, off + 76), u32(self.buf, off + 80)],
                "paramstart": [f32(self.buf, off + 84), f32(self.buf, off + 88)],
                "paramend": [f32(self.buf, off + 92), f32(self.buf, off + 96)],
                "paramparent": u32(self.buf, off + 100),
                "fadeintime": f32(self.buf, off + 104),
                "fadeouttime": f32(self.buf, off + 108),
                "cycleposeindex": u32(self.buf, off + 180),
                "numiklocks": u32(self.buf, off + 164),
                "iklockindex": u32(self.buf, off + 168),
                "numautolayers": u32(self.buf, off + 148),
                "autolayerindex": u32(self.buf, off + 152),
                "numactivitymodifiers": u32(self.buf, off + 188),
            })
        return out

    # -- pose parameters ----------------------------------------------------

    def pose_parameters(self):
        base = self.hdr["localposeparamindex"]
        n = self.hdr["numlocalposeparameters"]
        out = []
        for i in range(n):
            off = base + i * 20
            if off + 20 > self.size:
                self.warnings.append("pose param %d extends past EOF" % i)
                break
            szname = u32(self.buf, off)
            out.append({
                "index": i,
                "name": self.cstr(off + szname) if szname else "",
                "flags": u32(self.buf, off + 4),
                "min": f32(self.buf, off + 8),
                "max": f32(self.buf, off + 12),
                "loop": f32(self.buf, off + 16),
            })
        return out

    # -- IK chains ----------------------------------------------------------

    def ik_chains(self):
        base = self.hdr["ikchainindex"]
        n = self.hdr["numikchains"]
        bones = self.bones()
        out = []
        for i in range(n):
            off = base + i * 16
            if off + 16 > self.size:
                self.warnings.append("ik chain %d extends past EOF" % i)
                break
            szname = u32(self.buf, off)
            linktype = u32(self.buf, off + 4)
            numlinks = u32(self.buf, off + 8)
            linkindex = u32(self.buf, off + 12)
            links = []
            # NOTE: linkindex is *self-relative*: the engine's pLink() computes
            # ((byte*)this) + linkindex, i.e. relative to the containing chain
            # struct, NOT to the file start.  Verified against raw bytes.
            for j in range(numlinks):
                loff = off + linkindex + j * 28
                if loff + 28 > self.size:
                    self.warnings.append("ik chain %d link %d past EOF" % (i, j))
                    break
                bone = u32(self.buf, loff)
                links.append({
                    "bone": bone,
                    "bone_name": self.bone_name(bone, bones),
                    "kneeDir": [f32(self.buf, loff + 4), f32(self.buf, loff + 8),
                                f32(self.buf, loff + 12)],
                })
            out.append({
                "index": i,
                "name": self.cstr(off + szname) if szname else "",
                "linktype": linktype,
                "numlinks": numlinks,
                "links": links,
            })
        return out

    # -- IK autoplay locks ($ikautoplaylock) --------------------------------

    def ik_autoplay_locks(self):
        """Header-level locks: mstudioiklock_t { chain, flPosWeight, flLocalQWeight, flags, unused[4] } = 32 bytes.
        These are what `$ikautoplaylock` in a QC produces."""
        base = self.hdr["localikautoplaylockindex"]
        n = self.hdr["numlocalikautoplaylocks"]
        chains = self.ik_chains()
        out = []
        for i in range(n):
            off = base + i * 32
            if off + 32 > self.size:
                self.warnings.append("ik autoplay lock %d past EOF" % i)
                break
            chain = u32(self.buf, off)
            out.append({
                "index": i,
                "chain": chain,
                "chain_name": chains[chain]["name"] if 0 <= chain < len(chains) else "?",
                "flPosWeight": f32(self.buf, off + 4),
                "flLocalQWeight": f32(self.buf, off + 8),
                "flags": u32(self.buf, off + 12),
            })
        return out

    def seq_ik_locks(self):
        """Per-sequence locks (mstudioseqdesc_t.numiklocks/iklockindex)."""
        out = []
        base = self.hdr["localseqindex"]
        for s in self.sequences():
            if s["numiklocks"] <= 0:
                continue
            # iklockindex is self-relative to the seqdesc (same idiom as linkindex)
            seq_off = base + s["index"] * SEQDESC_SIZE
            locks = []
            for j in range(s["numiklocks"]):
                off = seq_off + s["iklockindex"] + j * 32
                if off + 32 > self.size:
                    break
                locks.append({
                    "chain": u32(self.buf, off),
                    "flPosWeight": f32(self.buf, off + 4),
                    "flLocalQWeight": f32(self.buf, off + 8),
                    "flags": u32(self.buf, off + 12),
                })
            out.append({"sequence": s["index"], "label": s["label"], "locks": locks})
        return out

    # -- include models -----------------------------------------------------

    def include_models(self):
        base = self.hdr["includemodelindex"]
        n = self.hdr["numincludemodels"]
        out = []
        for i in range(n):
            off = base + i * 8
            if off + 8 > self.size:
                self.warnings.append("include model %d past EOF" % i)
                break
            szlabel = u32(self.buf, off)
            szname = u32(self.buf, off + 4)
            out.append({
                "index": i,
                "label": self.cstr(off + szlabel) if szlabel else "",
                "name": self.cstr(off + szname) if szname else "",
            })
        return out

    def keyvalues(self):
        kv = self.hdr["keyvalueindex"]
        kvs = self.hdr["keyvaluesize"]
        if kvs <= 0 or kv <= 0 or kv + kvs > self.size:
            return ""
        return self.buf[kv:kv + kvs].split(b"\x00")[0].decode("ascii", "replace")

    # -- validation ---------------------------------------------------------

    def self_check(self):
        """Return a list of hard problems.  Empty list == parsing looks sane."""
        problems = []
        seqs = self.sequences()

        if self.numlocalseq > 0 and len(seqs) != self.numlocalseq:
            problems.append("read %d of %d sequences" % (len(seqs), self.numlocalseq))

        unnamed = [s for s in seqs if not s["activity_name"]]
        if self.numlocalseq and len(unnamed) == len(seqs):
            problems.append("no sequence has an activity name (layout suspect)")

        bad_labels = [s for s in seqs if s["label"] and not self.printable(s["label"])]
        if bad_labels:
            problems.append("%d sequence labels are not printable ASCII: %r"
                            % (len(bad_labels), bad_labels[0]["label"][:32]))

        bad_acts = [s for s in seqs
                    if s["activity_name"] and not s["activity_name"].startswith("ACT_")]
        if bad_acts:
            problems.append("%d activity names do not start with ACT_: %r"
                            % (len(bad_acts), bad_acts[0]["activity_name"][:48]))

        for s in seqs:
            if s["activity_name"] and not self.printable(s["activity_name"]):
                problems.append("non-printable activity name in seq %d" % s["index"])
                break

        for p in self.pose_parameters():
            if p["name"] and not self.printable(p["name"]):
                problems.append("non-printable pose param name at index %d" % p["index"])
                break
            if p["min"] > p["max"]:
                problems.append("pose param %r has min>max (%g>%g)"
                                % (p["name"], p["min"], p["max"]))

        for c in self.ik_chains():
            if c["name"] and not self.printable(c["name"]):
                problems.append("non-printable ik chain name at index %d" % c["index"])
                break
            if c["numlinks"] != len(c["links"]):
                problems.append("ik chain %r: %d links expected, %d read"
                                % (c["name"], c["numlinks"], len(c["links"])))
            for l in c["links"]:
                if l["bone_name"].startswith("<bone"):
                    problems.append("ik chain %r references bad bone %d"
                                    % (c["name"], l["bone"]))

        for b in self.bones():
            if not self.printable(b["name"]):
                problems.append("non-printable bone name at index %d" % b["index"])
                break

        return problems

    # -- reporting ----------------------------------------------------------

    def to_dict(self):
        seqs = self.sequences()
        acts = {}
        for s in seqs:
            key = s["activity_name"] or "<none>"
            e = acts.setdefault(key, {"activity_name": key, "numbers": {}, "sequences": []})
            e["numbers"][str(s["activity"])] = e["numbers"].get(str(s["activity"]), 0) + 1
            e["sequences"].append(s["index"])
        return {
            "path": self.path,
            "file_size": self.size,
            "model_name": self.name,
            "version": self.version,
            "checksum": self.checksum,
            "hdr_id": self.id,
            "length_field": self.length,
            "flags": self.flags,
            "numbones": self.numbones,
            "numlocalseq": self.numlocalseq,
            "numlocalanim": self.hdr["numlocalanim"],
            "numlocalposeparameters": self.hdr["numlocalposeparameters"],
            "numikchains": self.hdr["numikchains"],
            "numlocalikautoplaylocks": self.hdr["numlocalikautoplaylocks"],
            "studiohdr2index": self.studiohdr2index,
            "include_models": self.include_models(),
            "keyvalues": self.keyvalues(),
            "sequences": seqs,
            "activities": acts,
            "pose_parameters": self.pose_parameters(),
            "ik_chains": self.ik_chains(),
            "ik_autoplay_locks": self.ik_autoplay_locks(),
            "seq_ik_locks": self.seq_ik_locks(),
            "self_check_problems": self.self_check(),
            "warnings": self.warnings,
        }


# ---------------------------------------------------------------------------
# Text report
# ---------------------------------------------------------------------------

def report(m, show_all_activities=True):
    L = []
    a = L.append
    a("=" * 78)
    a("MODEL: %s" % m.name)
    a("  file            : %s" % m.path)
    a("  file size       : %d bytes" % m.size)
    a("  header length   : %d  %s" % (m.length, "OK" if m.length == m.size else "MISMATCH!"))
    a("  version         : %d" % m.version)
    a("  checksum        : 0x%08X" % (m.checksum & 0xFFFFFFFF))
    a("  header flags    : 0x%08X" % (m.flags & 0xFFFFFFFF))
    a("  studiohdr2index : %d %s" % (m.studiohdr2index,
                                    "(present)" if m.studiohdr2index else "(absent)"))
    a("  numbones        : %d" % m.numbones)
    a("  numlocalanim    : %d" % m.hdr["numlocalanim"])
    a("  numlocalseq     : %d" % m.numlocalseq)
    a("  numposeparams   : %d" % m.hdr["numlocalposeparameters"])
    a("  numikchains     : %d" % m.hdr["numikchains"])
    a("  numikautoplaylocks: %d" % m.hdr["numlocalikautoplaylocks"])
    a("  keyvalues       : %r" % m.keyvalues())

    inc = m.include_models()
    a("")
    a("--- $includemodel (%d) ---" % len(inc))
    if not inc:
        a("  (none)")
    for i in inc:
        a("  [%d] label=%-24r name=%s" % (i["index"], i["label"], i["name"]))

    # ---- activities ----
    seqs = m.sequences()
    acts = {}
    for s in seqs:
        key = s["activity_name"] or "<none>"
        e = acts.setdefault(key, {"nums": set(), "seqs": []})
        e["nums"].add(s["activity"])
        e["seqs"].append(s["index"])
    a("")
    a("--- ACTIVITIES: %d distinct names over %d sequences ---" % (len(acts), len(seqs)))
    if show_all_activities:
        for key in sorted(acts.keys()):
            e = acts[key]
            nums = ",".join(str(n) for n in sorted(e["nums"]))
            a("  %-42s activity=%-6s seqs(%2d)=%s"
              % (key, nums, len(e["seqs"]),
                 ",".join(str(x) for x in e["seqs"][:40])
                 + ("..." if len(e["seqs"]) > 40 else "")))
    prefixed = sum(1 for k in acts if k.startswith("ACT_"))
    a("  names starting with 'ACT_' : %d / %d" % (prefixed, len(acts)))
    hl2mp = sorted(k for k in acts if k.startswith("ACT_HL2MP_"))
    gmod = sorted(k for k in acts if k.startswith("ACT_GMOD"))
    flinch = sorted(k for k in acts if k.startswith("ACT_FLINCH"))
    other = sorted(k for k in acts
                   if not k.startswith(("ACT_HL2MP_", "ACT_GMOD", "ACT_FLINCH")))
    a("  ACT_HL2MP_* : %d   ACT_GMOD* : %d   ACT_FLINCH* : %d   other : %d"
      % (len(hl2mp), len(gmod), len(flinch), len(other)))
    if other:
        a("  other names: %s" % ", ".join(other))

    # ---- sequence list ----
    a("")
    a("--- SEQUENCES (%d) ---" % len(seqs))
    poses = m.pose_parameters()
    pnames = [p["name"] for p in poses]
    for s in seqs:
        pi = s["paramindex"]
        pdesc = ""
        if s["numblends"] > 0:
            names = []
            for k in (0, 1):
                idx = pi[k]
                if 0 <= idx < len(pnames):
                    names.append("%s[%.2f..%.2f]" % (pnames[idx], s["paramstart"][k],
                                                     s["paramend"][k]))
                else:
                    names.append("param%d=?" % idx)
            pdesc = "  blends=%dx%d params=(%s)" % (
                s["groupsize"][0], s["groupsize"][1], ", ".join(names))
        a("  [%3d] %-34s act=%-5d %-40s%s"
          % (s["index"], s["label"][:34], s["activity"],
             s["activity_name"][:40], pdesc))

    # ---- pose parameters ----
    a("")
    a("--- POSE PARAMETERS (%d) ---" % len(poses))
    for p in poses:
        a("  [%2d] %-18s min=%-10g max=%-10g loop=%-8g flags=0x%X"
          % (p["index"], p["name"], p["min"], p["max"], p["loop"], p["flags"]))
    have = set(p["name"] for p in poses)
    for probe in ("move_x", "move_y", "aim_yaw", "aim_pitch", "body_yaw",
                  "head_yaw", "head_pitch", "body_pitch", "spine_yaw", "spine_pitch",
                  "gesture", "aim_1", "aim_2"):
        if probe in have:
            pp = [p for p in poses if p["name"] == probe][0]
            a("  PROBE %-11s : PRESENT min=%g max=%g loop=%g" % (probe, pp["min"], pp["max"], pp["loop"]))
        else:
            a("  PROBE %-11s : absent" % probe)

    # ---- IK chains ----
    a("")
    a("--- IK CHAINS (numikchains=%d) ---" % m.hdr["numikchains"])
    for c in m.ik_chains():
        a("  [%d] %-20s linktype=%d numlinks=%d" % (c["index"], c["name"], c["linktype"],
                                                   c["numlinks"]))
        for l in c["links"]:
            a("        bone[%3d] %-24s kneeDir=(%g, %g, %g)"
              % (l["bone"], l["bone_name"], l["kneeDir"][0], l["kneeDir"][1], l["kneeDir"][2]))

    locks = m.ik_autoplay_locks()
    a("")
    a("--- $ikautoplaylock (header-level, numlocalikautoplaylocks=%d) ---"
      % m.hdr["numlocalikautoplaylocks"])
    if not locks:
        a("  (none)")
    for l in locks:
        a("  [%d] chain=%d (%s) flPosWeight=%g flLocalQWeight=%g flags=0x%X"
          % (l["index"], l["chain"], l["chain_name"], l["flPosWeight"],
             l["flLocalQWeight"], l["flags"]))

    slocks = m.seq_ik_locks()
    a("--- per-sequence ik locks: %d sequence(s) ---" % len(slocks))
    for s in slocks:
        a("  seq[%d] %s -> %s" % (s["sequence"], s["label"], s["locks"]))

    # ---- self check ----
    a("")
    probs = m.self_check()
    a("--- SELF-CHECK: %s ---" % ("PASS" if not probs else "FAIL"))
    for p in probs:
        a("  !! %s" % p)
    for w in m.warnings:
        a("  ~  %s" % w)
    a("")
    return "\n".join(L)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Inspect Source .mdl files (read-only).")
    ap.add_argument("models", nargs="+", help="path(s) to .mdl files")
    ap.add_argument("--json", metavar="OUT", help="write full parsed dump as JSON")
    ap.add_argument("--quiet", action="store_true", help="only print the summary block")
    args = ap.parse_args(argv)

    rc = 0
    dumps = []
    for path in args.models:
        print("### reading %s" % path)
        try:
            m = Mdl(path)
        except (MdlError, OSError) as exc:
            print("  FAILED to parse: %s" % exc)
            rc = 1
            continue
        d = m.to_dict()
        dumps.append(d)
        if args.quiet:
            print(report(m, show_all_activities=False))
        else:
            print(report(m))
        if d["self_check_problems"]:
            rc = 1

    if args.json:
        with open(args.json, "w") as fh:
            json.dump(dumps if len(dumps) != 1 else dumps[0], fh, indent=1, sort_keys=True)
        print("### wrote %s" % args.json)
    return rc


if __name__ == "__main__":
    sys.exit(main())
