#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mdl_activity_scan.py - scan every .mdl under the given roots and report, for
each sequence, the RAW `mstudioseqdesc_t.activity` integer together with the
activity NAME string.

Purpose: decide empirically whether GMod-compiled .mdl files actually store a
resolved activity NUMBER (which would let us recover GMod's closed-source
activity enum) or only the activity NAME (with activity == -1 on disk, resolved
at runtime by the game DLL from the name).

Read-only. Emits a CSV of every sequence record plus a console summary.

Usage:
  python mdl_activity_scan.py --csv out.csv ROOT [ROOT ...]
"""

import argparse
import csv
import os
import sys
import collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from studiomdl_inspect import Mdl, MdlError  # noqa: E402


def iter_mdl(roots):
    seen = set()
    for root in roots:
        if os.path.isfile(root):
            cands = [root]
        else:
            cands = []
            for dirpath, _dirnames, filenames in os.walk(root):
                for fn in filenames:
                    if fn.lower().endswith(".mdl"):
                        cands.append(os.path.join(dirpath, fn))
        for p in sorted(cands):
            rp = os.path.realpath(p)
            if rp in seen:
                continue
            seen.add(rp)
            yield p


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="+")
    ap.add_argument("--csv", default=None)
    ap.add_argument("--only-activities", action="store_true",
                    help="only emit sequences that have a non-empty activity name")
    args = ap.parse_args(argv)

    rows = []
    per_model = []
    failures = []
    for path in iter_mdl(args.roots):
        try:
            m = Mdl(path)
            seqs = m.sequences()
        except (MdlError, OSError, Exception) as exc:  # noqa: BLE001
            failures.append((path, str(exc)))
            continue
        act_vals = collections.Counter()
        for s in seqs:
            act_vals[s["activity"]] += 1
            if args.only_activities and not s["activity_name"]:
                continue
            rows.append({
                "model": m.name,
                "path": path,
                "version": m.version,
                "seq": s["index"],
                "label": s["label"],
                "activity_name": s["activity_name"],
                "activity": s["activity"],
                "flags": s["flags"],
                "numblends": s["numblends"],
            })
        per_model.append({
            "path": path,
            "name": m.name,
            "version": m.version,
            "numseq": m.numlocalseq,
            "numbones": m.numbones,
            "numikchains": m.hdr["numikchains"],
            "numposeparams": m.hdr["numlocalposeparameters"],
            "activity_values": dict(act_vals),
            "nonzero_activity_count": sum(c for v, c in act_vals.items() if v != -1),
            "distinct_activity_names": len(set(s["activity_name"] for s in seqs)),
            "has_hl2mp": any(s["activity_name"].startswith("ACT_HL2MP_") for s in seqs),
        })

    # ---------------- summary ----------------
    print("=" * 78)
    print("SCANNED %d models (%d unparseable)" % (len(per_model), len(failures)))
    allvals = collections.Counter()
    for pm in per_model:
        for v, c in pm["activity_values"].items():
            allvals[v] += c
    print("RAW activity values over ALL sequences: %s"
          % dict(sorted(allvals.items())))
    nz = [pm for pm in per_model if pm["nonzero_activity_count"]]
    print("models with ANY activity != -1: %d" % len(nz))
    for pm in nz[:40]:
        print("   %s  v%d seq=%d  %s" % (pm["path"], pm["version"], pm["numseq"],
                                        pm["activity_values"]))

    print()
    print("--- models that bind ACT_HL2MP_* (%d) ---" % sum(1 for p in per_model if p["has_hl2mp"]))
    for pm in per_model:
        if pm["has_hl2mp"]:
            print("   %-70s v%d seq=%d activityvals=%s" % (pm["path"], pm["version"],
                                                           pm["numseq"], pm["activity_values"]))

    print()
    print("--- all models ---")
    for pm in sorted(per_model, key=lambda x: x["path"]):
        print("   %-72s v%-3d seq=%-4d bones=%-3d ik=%-2d pp=%-2d actvals=%s"
              % (pm["path"], pm["version"], pm["numseq"], pm["numbones"],
                 pm["numikchains"], pm["numposeparams"], pm["activity_values"]))
    if failures:
        print()
        print("--- unparseable ---")
        for p, e in failures:
            print("   %s : %s" % (p, e))

    if args.csv:
        d = os.path.dirname(os.path.abspath(args.csv))
        if d and not os.path.isdir(d):
            os.makedirs(d)
        with open(args.csv, "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=["model", "path", "version", "seq", "label",
                                               "activity_name", "activity", "flags",
                                               "numblends"])
            w.writeheader()
            for r in rows:
                w.writerow(r)
        print()
        print("### wrote %s (%d sequence rows)" % (args.csv, len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
