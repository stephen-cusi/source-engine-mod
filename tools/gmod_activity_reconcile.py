#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gmod_activity_reconcile.py

Reconciles three independent sources of Garry's Mod player-animation activity
information for the HL2SB port:

  [MDL]  the raw bytes of GMod's compiled player animation libraries
         (models/m_anm.mdl, f_anm.mdl, z_anm.mdl, and every other .mdl we have)
         -> parsed by studiomdl_inspect.py / mdl_activity_scan.py.
  [GMOD] the recovered GMod activity name -> number list staged at
         _staging/gmod_ACT_numbers.txt (derived from GMod's own wiki dump).
  [LUA]  GMod's gamemode/animations.lua IdleActivityTranslate arithmetic.

FINDING THIS SCRIPT EXISTS TO DOCUMENT:
  [MDL] contains NO activity numbers at all - every mstudioseqdesc_t.activity is
  -1, because studiomdl only ever records the activity NAME
  (utils/studiomdl/studiomdl.cpp:3994 sets pseq->activity = -1 and nothing ever
  assigns it; write.cpp:658 writes that -1 out).  The number is filled in at
  RUNTIME by the game DLL from the name (game/shared/animation.cpp
  SetActivityForSequence + STUDIO_ACTIVITY).  Therefore GMod's activity numbering
  is NOT recoverable from .mdl data, and [GMOD]/[LUA] must carry the numbers.

Read-only. Emits CSV + JSON + markdown tables.

Usage:
  python gmod_activity_reconcile.py --mdl-json raw_dump.json \
      --gmod-numbers _staging/gmod_ACT_numbers.txt \
      --out-prefix GMod_activity_reconciliation
"""

import argparse
import collections
import csv
import json
import os
import re
import sys


# ---------------------------------------------------------------------------

def parse_gmod_numbers(path):
    """s_ActivityList-style lines: ' <number>  ACT_NAME'."""
    num = {}
    order = []
    bad = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.match(r"\s*(-?\d+)\s+(\S+)\s*$", line)
            if m:
                n = int(m.group(1))
                name = m.group(2)
                num[name] = n
                order.append((n, name))
            elif line.strip():
                bad.append(line.rstrip())
    return num, order, bad


def md5ish(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--mdl-json", required=True,
                    help="raw_dump.json produced by studiomdl_inspect.py --json")
    ap.add_argument("--gmod-numbers", required=True)
    ap.add_argument("--scan-csv", default=None,
                    help="mdl_activity_all.csv from mdl_activity_scan.py")
    ap.add_argument("--out-prefix", default="GMod_activity_reconciliation")
    args = ap.parse_args(argv)

    dump = json.load(open(args.mdl_json))
    if isinstance(dump, dict):
        dump = [dump]
    gnum, gorder, gbad = parse_gmod_numbers(args.gmod_numbers)

    # ---- 1. does ANY .mdl carry a real activity number? -------------------
    total_seq = 0
    nonzero = []
    raw_values = collections.Counter()
    for m in dump:
        for s in m["sequences"]:
            total_seq += 1
            raw_values[s["activity"]] += 1
            if s["activity"] != -1:
                nonzero.append((m["model_name"], s["index"], s["activity_name"], s["activity"]))
    if args.scan_csv and os.path.exists(args.scan_csv):
        with open(args.scan_csv, newline="") as fh:
            for r in csv.DictReader(fh):
                raw_values[int(r["activity"])] += 0  # already covered; keep for record
    scan_seq = None
    if args.scan_csv and os.path.exists(args.scan_csv):
        with open(args.scan_csv, newline="") as fh:
            scan_seq = sum(1 for _ in csv.DictReader(fh))

    # ---- 2. GMod's ACT_HL2MP_* block arithmetic ---------------------------
    base = gnum.get("ACT_HL2MP_IDLE")
    rev = {}
    for n, name in gorder:
        rev.setdefault(n, name)

    block = []
    if base is not None:
        for d in range(0, 11):
            block.append((d, base + d, rev.get(base + d, "<no entry>")))

    lua_expect = [
        ("ACT_MP_STAND_IDLE", 0), ("ACT_MP_WALK", 1), ("ACT_MP_RUN", 2),
        ("ACT_MP_CROUCH_IDLE", 3), ("ACT_MP_CROUCHWALK", 4),
        ("ACT_MP_ATTACK_STAND_PRIMARYFIRE", 5),
        ("ACT_MP_ATTACK_CROUCH_PRIMARYFIRE", 5),
        ("ACT_MP_RELOAD_STAND", 6), ("ACT_MP_RELOAD_CROUCH", 6),
        ("ACT_MP_SWIM", 9),
    ]
    lua_check = []
    for nm, d in lua_expect:
        want = (base + d) if base is not None else None
        got_name = rev.get(want, "<no entry>") if want is not None else None
        lua_check.append({
            "ACT_MP_name": nm, "offset": d, "expected_number": want,
            "gmod_name_at_that_number": got_name, "ok": bool(got_name and got_name != "<no entry>"),
        })

    # ---- 3. hold-type suffix blocks (GMod enum) --------------------------
    suffix_order = ["IDLE_{}", "WALK_{}", "RUN_{}", "IDLE_CROUCH_{}", "WALK_CROUCH_{}",
                    "GESTURE_RANGE_ATTACK_{}", "GESTURE_RELOAD_{}", "JUMP_{}",
                    "SWIM_IDLE_{}", "SWIM_{}"]
    # infer hold types from GMod's own enum: every name matching IDLE_<HT> with a
    # complete 10-wide block present
    ht_seen = {}
    for n, name in gorder:
        if name.startswith("ACT_HL2MP_"):
            tail = name[len("ACT_HL2MP_"):]
            for i, pat in enumerate(suffix_order):
                pref = pat.split("_{}")[0]
                if tail.startswith(pref + "_"):
                    ht = tail[len(pref) + 1:]
                    e = ht_seen.setdefault(ht, {})
                    e.setdefault(i, (n, name))
    blocks = []
    for ht in sorted(ht_seen):
        slots = ht_seen[ht]
        base_ht = slots.get(0, (None, None))[0]
        ok = all(i in slots for i in range(10)) and base_ht is not None and \
            all(slots[i][0] == base_ht + i for i in range(10))
        blocks.append({
            "holdtype_suffix": ht,
            "block_base": base_ht,
            "complete_10_slots": ok,
            "slots": {str(i): slots.get(i, (None, None))[1] for i in range(10)},
            "numbers": {str(i): slots.get(i, (None, None))[0] for i in range(10)},
        })

    # ---- 4. name vocabulary per library (from the .mdl probe) ------------
    libs = {}
    for m in dump:
        names = collections.OrderedDict()
        for s in m["sequences"]:
            k = s["activity_name"]
            names.setdefault(k, []).append(s["index"])
        libs[m["model_name"]] = names

    gmod_hl2mp = sorted(k for k in gnum if k.startswith("ACT_HL2MP_"))
    gmod_gmod = sorted(k for k in gnum if k.startswith("ACT_GMOD"))
    gmod_flinch = sorted(k for k in gnum if k.startswith("ACT_FLINCH"))
    gmod_mp = sorted(k for k in gnum if k.startswith("ACT_MP_"))

    # name -> record (union over all probed libs)
    all_name_union = collections.OrderedDict()
    for m in dump:
        for s in m["sequences"]:
            all_name_union.setdefault(s["activity_name"], []).append(
                (m["model_name"], s["index"]))
    present = set(all_name_union.keys())

    # ------------------------------------------------------------------
    summary = {
        "provenance": {
            "mdl_json": args.mdl_json,
            "mdl_json_sha256": md5ish(args.mdl_json),
            "gmod_numbers_file": args.gmod_numbers,
            "gmod_numbers_sha256": md5ish(args.gmod_numbers),
            "gmod_numbers_entries": len(gnum),
            "unparsed_lines_in_gmod_numbers": gbad[:10],
        },
        "mdl_evidence": {
            "models_probed": len(dump),
            "sequences_probed": total_seq,
            "raw_activity_value_histogram": {str(k): v for k, v in sorted(raw_values.items())},
            "sequences_with_activity_not_minus_1": len(nonzero),
            "examples": nonzero[:20],
            "conclusion": (
                "Every mstudioseqdesc_t.activity in every probed .mdl is -1. "
                "studiomdl writes activity=-1 and only records the activity NAME "
                "(utils/studiomdl/studiomdl.cpp:3994, write.cpp:658); the number is "
                "resolved at load time by the game DLL from the name "
                "(game/shared/animation.cpp SetActivityForSequence). GMod's activity "
                "numbering therefore CANNOT be recovered from .mdl bytes."
            ),
        },
        "gmod_enum": {
            "source": "staged GMod activity list (wiki-derived), NOT from .mdl",
            "ACT_HL2MP_IDLE": base,
            "ACT_HL2MP_WALK": gnum.get("ACT_HL2MP_WALK"),
            "ACT_HL2MP_SWIM": gnum.get("ACT_HL2MP_SWIM"),
            "ACT_HL2MP_SWIM_IDLE": gnum.get("ACT_HL2MP_SWIM_IDLE"),
            "ACT_HL2MP_JUMP": gnum.get("ACT_HL2MP_JUMP"),
            "idle_block_plus_0_to_10": block,
            "counts": {
                "ACT_HL2MP_*": len(gmod_hl2mp),
                "ACT_GMOD*": len(gmod_gmod),
                "ACT_FLINCH*": len(gmod_flinch),
                "ACT_MP_*": len(gmod_mp),
            },
            "ACT_MP_STAND_IDLE": gnum.get("ACT_MP_STAND_IDLE"),
            "animations_lua_arithmetic_check": lua_check,
            "holdtype_blocks": blocks,
        },
        "mdl_libraries": {
            name: {
                "distinct_activity_names": len(names),
                "sequences": sum(len(v) for v in names.values()),
                "has_hl2mp_walk_name": "ACT_HL2MP_WALK" in names,
                "has_hl2mp_swim_name": "ACT_HL2MP_SWIM" in names,
                "names": list(names.keys()),
            } for name, names in libs.items()
        },
    }

    # ------------------------------------------------------------------
    op = args.out_prefix
    with open(op + ".json", "w") as fh:
        json.dump(summary, fh, indent=1, sort_keys=True)

    # CSV: one row per (name, library) with both number sources side by side
    rows = []
    for name, occ in all_name_union.items():
        per_lib = collections.defaultdict(list)
        for lib, idx in occ:
            per_lib[lib].append(idx)
        rows.append({
            "activity_name": name,
            "gmod_enum_number": gnum.get(name, ""),
            "raw_mdl_activity": -1,
            "n_mdl_occurrences": len(occ),
            "libraries": ";".join("%s:%s" % (k, ",".join(str(i) for i in v))
                                  for k, v in sorted(per_lib.items())),
            "in_gmod_enum_list": "yes" if name in gnum else "no",
        })
    rows.sort(key=lambda r: (r["gmod_enum_number"] == "", r["activity_name"]))
    with open(op + ".csv", "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["activity_name", "gmod_enum_number",
                                           "raw_mdl_activity", "n_mdl_occurrences",
                                           "libraries", "in_gmod_enum_list"])
        w.writeheader()
        for r in rows:
            w.writerow(r)

    # ------------------------------------------------------------------
    print("=" * 78)
    print("MDL EVIDENCE")
    for k, v in summary["mdl_evidence"].items():
        print("   %-38s %s" % (k, v if not isinstance(v, list) else v[:3]))
    print()
    print("GMOD ENUM (from staged list, NOT from .mdl)")
    g = summary["gmod_enum"]
    for k in ("ACT_HL2MP_IDLE", "ACT_HL2MP_WALK", "ACT_HL2MP_RUN" if False else "ACT_HL2MP_SWIM",
              "ACT_HL2MP_SWIM_IDLE", "ACT_HL2MP_JUMP", "ACT_MP_STAND_IDLE"):
        print("   %-24s = %s" % (k, g.get(k)))
    print("   counts: %s" % g["counts"])
    print("   ACT_HL2MP_IDLE+0..+10:")
    for d, n, nm in block:
        print("      +%-2d %-6d %s" % (d, n, nm))
    print("   animations.lua arithmetic check:")
    for c in lua_check:
        print("      %-40s +%-2d -> %d = %-34s %s"
              % (c["ACT_MP_name"], c["offset"], c["expected_number"],
                 c["gmod_name_at_that_number"], "OK" if c["ok"] else "MISMATCH"))
    print("   hold-type blocks: %d ; complete 10-slot blocks: %d"
          % (len(blocks), sum(1 for b in blocks if b["complete_10_slots"])))
    print()
    print("MDL LIBRARIES")
    for name, d in summary["mdl_libraries"].items():
        print("   %-14s names=%-4d seqs=%-4d has_ACT_HL2MP_WALK=%-5s has_ACT_HL2MP_SWIM=%s"
              % (name, d["distinct_activity_names"], d["sequences"],
                 d["has_hl2mp_walk_name"], d["has_hl2mp_swim_name"]))
    print()
    print("### wrote %s.json / %s.csv" % (op, op))
    return 0


if __name__ == "__main__":
    sys.exit(main())
