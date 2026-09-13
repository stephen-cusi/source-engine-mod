#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gmod_activity_absent_names.py

Answers the one question the Stage 0 fix depends on: which activity NAMES bound
by GMod's player animation libraries (models/m_anm.mdl, f_anm.mdl, z_anm.mdl)
are ABSENT from this fork's shared activity enum in game/shared/ai_activity.h?

Why this is the right question: mstudioseqdesc_t.activity is -1 in every
compiled .mdl, so the number is synthesised at load time by the game DLL from
the name (game/shared/animation.cpp:140 SetActivityForSequence ->
ActivityList_IndexForName). A name that is missing from the shared list makes
the server and the client disagree (server registers a PRIVATE activity, client
clears STUDIO_ACTIVITY and keeps the raw value), so the missing names - not any
number translation - are the real work item.

Method: the names are taken from the parsed .mdl bytes (parsed by
studiomdl_inspect.py); the enum vocabulary is read textually out of
game/shared/ai_activity.h, one enum-member identifier per line. Nothing is taken
from memory, and no engine file is modified.

Read-only.

Usage:
  python gmod_activity_absent_names.py --mdl-json raw_dump.json \
      --activity-header ../../game/shared/ai_activity.h \
      --out-prefix GMod_activity_absent_names
"""

import argparse
import collections
import csv
import json
import os
import re
import sys

FAMILIES = [
    ("ACT_HL2MP_", "ACT_HL2MP_*"),
    ("ACT_GMOD", "ACT_GMOD_*"),
    ("ACT_FLINCH", "ACT_FLINCH*"),
]


def family_of(name):
    for pref, label in FAMILIES:
        if name.startswith(pref):
            return label
    return "other (Valve ACT_*)"


def parse_enum_members(header_path):
    """Return (ordered_all_identifiers, set_of_identifiers).

    Only lines that are bare enum members are taken, so identifiers appearing in
    comments or in unrelated expressions do not leak in. Conditional blocks are
    recorded so the report can say whether a name is unconditional.
    """
    ordered = []
    cond_stack = []
    member_cond = {}
    # One enum member per line; tolerate an explicit value ("ACT_RESET = 0,")
    # and a trailing // comment (which may itself mention another ACT_ name).
    line_re = re.compile(r"^\s*(ACT_[A-Za-z0-9_]+)\s*(?:=\s*[^,]+)?\s*,?\s*(?://.*)?$")
    cond_re = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")
    with open(header_path, "r", encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            c = cond_re.match(raw)
            if c:
                kind, rest = c.group(1), c.group(2).strip()
                if kind in ("if", "ifdef", "ifndef"):
                    cond_stack.append("%s %s" % (kind, rest))
                elif kind == "elif":
                    if cond_stack:
                        cond_stack[-1] = "elif %s" % rest
                elif kind == "else":
                    if cond_stack:
                        cond_stack[-1] = "else(%s)" % cond_stack[-1]
                elif kind == "endif":
                    if cond_stack:
                        cond_stack.pop()
                continue
            m = line_re.match(raw)
            if m:
                name = m.group(1)
                ordered.append(name)
                if name not in member_cond:
                    member_cond[name] = " && ".join(cond_stack) if cond_stack else ""
    return ordered, set(ordered), member_cond


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--mdl-json", required=True)
    ap.add_argument("--activity-header", required=True)
    ap.add_argument("--out-prefix", default="GMod_activity_absent_names")
    ap.add_argument("--libraries", default="m_anm.mdl,f_anm.mdl,z_anm.mdl",
                    help="comma-separated model_name values to take names from")
    args = ap.parse_args(argv)

    libs = [s.strip() for s in args.libraries.split(",") if s.strip()]

    dump = json.load(open(args.mdl_json))
    if isinstance(dump, dict):
        dump = [dump]

    # ---- names bound by the GMod animation libraries, with their sequences --
    per_name = collections.OrderedDict()
    for m in dump:
        if m["model_name"] not in libs:
            continue
        for s in m["sequences"]:
            e = per_name.setdefault(s["activity_name"], {"seqs": [], "libs": set()})
            e["seqs"].append((m["model_name"], s["index"], s["label"]))
            e["libs"].add(m["model_name"])

    real_names = sorted(n for n in per_name if n)
    empty_count = len(per_name) - len(real_names)

    ordered_enum, enum_names, member_cond = parse_enum_members(args.activity_header)

    present = [n for n in real_names if n in enum_names]
    absent = [n for n in real_names if n not in enum_names]

    by_family = collections.OrderedDict()
    for label in [l for _, l in FAMILIES] + ["other (Valve ACT_*)"]:
        fam_all = [n for n in real_names if family_of(n) == label]
        fam_absent = [n for n in fam_all if n in absent]
        by_family[label] = {
            "bound_in_libs": len(fam_all),
            "absent_from_our_enum": len(fam_absent),
            "names": fam_all,
            "absent_names": sorted(fam_absent),
        }

    result = {
        "provenance": {
            "mdl_json": args.mdl_json,
            "activity_header": args.activity_header,
            "libraries": libs,
        },
        "counts": {
            "distinct_names_in_libraries": len(per_name),
            "distinct_nonempty_names": len(real_names),
            "sequences_with_no_activity_name": sum(
                len(per_name[n]["seqs"]) for n in per_name if not n),
            "enum_members_in_our_header": len(ordered_enum),
            "enum_distinct_members": len(enum_names),
            "names_present_in_our_enum": len(present),
            "names_ABSENT_from_our_enum": len(absent),
        },
        "absent_names": sorted(absent),
        "present_names": sorted(present),
        "by_family": by_family,
        "enum_members_declared_only_under_a_condition": {
            n: member_cond[n] for n in sorted(enum_names) if member_cond.get(n)
        },
        "name_detail": {
            n: {
                "family": family_of(n),
                "in_our_enum": n in enum_names,
                "enum_condition": member_cond.get(n, None),
                "n_sequences": len(per_name[n]["seqs"]),
                "libraries": sorted(per_name[n]["libs"]),
                "sequences": [{"model": a, "seq": b, "label": c}
                              for a, b, c in per_name[n]["seqs"]],
            } for n in real_names
        },
    }

    op = args.out_prefix
    with open(op + ".json", "w") as fh:
        json.dump(result, fh, indent=1, sort_keys=True)

    with open(op + ".csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["activity_name", "family", "in_our_ai_activity_h",
                    "n_sequences", "libraries", "sequence_labels"])
        for n in real_names:
            d = result["name_detail"][n]
            w.writerow([n, d["family"],
                        "yes" if d["in_our_enum"] else "NO",
                        d["n_sequences"],
                        ";".join(d["libraries"]),
                        ";".join("%s#%d:%s" % (s["model"], s["seq"], s["label"])
                                 for s in d["sequences"])])
        w.writerow([])
        w.writerow(["# empty activity name (means 'no activity')",
                    "", "", result["counts"]["sequences_with_no_activity_name"], "", ""])

    # ---------------- console ----------------
    c = result["counts"]
    print("=" * 78)
    print("GMod animation libraries     : %s" % ", ".join(libs))
    print("our shared enum header       : %s" % args.activity_header)
    print("distinct names in libraries  : %d  (real: %d, empty string: 1 group over %d seqs)"
          % (c["distinct_names_in_libraries"], c["distinct_nonempty_names"],
             c["sequences_with_no_activity_name"]))
    print("enum members in our header   : %d (distinct %d)"
          % (c["enum_members_in_our_header"], c["enum_distinct_members"]))
    print("present in our enum          : %d" % c["names_present_in_our_enum"])
    print("ABSENT  from our enum        : %d" % c["names_ABSENT_from_our_enum"])
    print()
    for label, d in by_family.items():
        print("%-20s bound=%-4d absent=%-4d" % (label, d["bound_in_libs"],
                                                d["absent_from_our_enum"]))
    print()
    print("--- NAMES THAT MUST BE ADDED (%d) ---" % len(absent))
    for n in sorted(absent):
        d = result["name_detail"][n]
        print("   %-44s %-20s seqs=%d  %s"
              % (n, d["family"], d["n_sequences"], ",".join(d["libraries"])))
    print()
    cond = result["enum_members_declared_only_under_a_condition"]
    print("enum members that are conditional in our header: %d" % len(cond))
    print()
    print("### wrote %s.json / %s.csv" % (op, op))
    return 0


if __name__ == "__main__":
    sys.exit(main())
