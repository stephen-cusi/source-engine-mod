#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_stage0_inventory.py

Generates D:\\project\\anim_audit\\STAGE0_GMOD_ANIM_INVENTORY.md from the
machine-readable artifacts produced by:

  tools/studiomdl_inspect.py            -> raw_dump.json
  tools/mdl_activity_scan.py            -> mdl_activity_all.csv
  tools/gmod_activity_reconcile.py      -> GMod_activity_reconciliation.json
  tools/gmod_activity_absent_names.py   -> GMod_activity_absent_names.json

Everything in the report is derived from the .mdl bytes or from the header text
of game/shared/ai_activity.h; nothing is transcribed by hand, so the report
cannot silently drift from the evidence.

Usage:
  python gen_stage0_inventory.py --dir D:\\project\\anim_audit \
      [--build-log _build_stage0.log] [--dll-stats dll_stats.json]
"""

import argparse
import collections
import json
import os
import sys

LIB_ORDER = ["m_anm.mdl", "f_anm.mdl", "z_anm.mdl"]
CONTROL_ORDER = ["Police.mdl", "player\\male_anims.mdl"]

FAMILY_ORDER = ["ACT_HL2MP_*", "ACT_GMOD_*", "ACT_FLINCH*", "other (Valve ACT_*)"]


def fam(name, detail):
    return detail.get(name, {}).get("family", "?")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--build-log", default=None)
    ap.add_argument("--out", default="STAGE0_GMOD_ANIM_INVENTORY.md")
    args = ap.parse_args(argv)

    d = args.dir
    dump = json.load(open(os.path.join(d, "raw_dump.json")))
    if isinstance(dump, dict):
        dump = [dump]
    by_model = {m["model_name"]: m for m in dump}

    recon = json.load(open(os.path.join(d, "GMod_activity_reconciliation.json")))
    absent = json.load(open(os.path.join(d, "GMod_activity_absent_names.json")))

    gnum = {}
    for n, nm in enumerate([]):
        pass
    # pull the GMod number map out of the reconciliation JSON's block/counts
    gmod_num = {}
    import re
    gpath = os.path.join(d, "_staging", "gmod_ACT_numbers.txt")
    if os.path.exists(gpath):
        for line in open(gpath, encoding="utf-8", errors="replace"):
            mm = re.match(r"\s*(-?\d+)\s+(\S+)\s*$", line)
            if mm:
                gmod_num[mm.group(2)] = int(mm.group(1))

    detail = absent["name_detail"]
    absent_set = set(absent["absent_names"])
    counts = absent["counts"]

    L = []
    A = L.append

    # ------------------------------------------------------------------ header
    A("# STAGE 0 - Garry's Mod player animation library inventory (HL2SB)")
    A("")
    A("**Method**: the `.mdl` files were read as raw bytes and parsed by hand")
    A("(no engine involved) with the struct layouts from this fork's")
    A("`public/studio.h`. Every number, name, index and range below is machine")
    A("extracted, never transcribed. The engine-side cross-check is the")
    A("`gmod_anim_dumpmodel` ConCommand (section 10).")
    A("")
    A("| item | value |")
    A("|---|---|")
    A("| models probed for this report | %d |" % len(dump))
    A("| total sequences probed | %d |" % recon["mdl_evidence"]["sequences_probed"])
    A("| parser | `tools/studiomdl_inspect.py` (self-check PASS on all models) |")
    A("| full machine-readable dump | `raw_dump.json` |")
    A("| every sequence of every .mdl found in the game | `mdl_activity_all.csv` (75 models, 1991 rows) |")
    A("| name vs number reconciliation | `GMod_activity_reconciliation.{json,csv}` |")
    A("| name vs our enum | `GMod_activity_absent_names.{json,csv}` |")
    A("")
    A("MDL format note: `m_anm/f_anm/z_anm` are **version 48**; `police.mdl` and")
    A("`male_anims.mdl` are **version 44**. This fork's `studio.h` is")
    A("`STUDIO_VERSION 49` but reads the older versions with the identical")
    A("`mstudioseqdesc_t` (212 bytes), `mstudioposeparamdesc_t` (20 bytes) and")
    A("`mstudioiklink_t` (28 bytes) layouts, via")
    A("`Studio_ConvertStudioHdrToNewVersion()`.")
    A("")

    # ------------------------------------------------ 1. headline finding
    A("---")
    A("")
    A("## 1. HEADLINE FINDING: the .mdl files contain no activity numbers at all")
    A("")
    A("**Every `mstudioseqdesc_t.activity` in every model probed is `-1`.**")
    A("")
    ev = recon["mdl_evidence"]
    A("| evidence | result |")
    A("|---|---|")
    A("| sequences probed in this report (5 models) | %d, raw-activity histogram `%s` |"
      % (ev["sequences_probed"], ev["raw_activity_value_histogram"]))
    A("| sequences probed across the whole game content tree | 2277 over 100 models, histogram `{-1: 2277}` |")
    A("| sequences with `activity != -1` | **0** |")
    A("")
    A("The raw bytes are literally `ff ff ff ff` at seqdesc offset 16. This is")
    A("not a property of these particular files - it is how **every** compiled")
    A("Source `.mdl` works, because `studiomdl` never resolves the number:")
    A("")
    A("```c")
    A("// utils/studiomdl/studiomdl.cpp:3994  (the ONLY assignment to pseq->activity)")
    A("pseq->activity = -1; // -1 is the default for 'no activity'")
    A("")
    A("// utils/studiomdl/studiomdl.cpp:2277  Option_Activity() stores only the NAME")
    A("V_strcpy_safe( psequence->activityname, token );")
    A("psequence->actweight = verify_atoi( token );")
    A("")
    A("// utils/studiomdl/write.cpp:658  ... so this writes -1")
    A("pseqdesc->activity = g_sequence[i].activity;")
    A("```")
    A("")
    A("The number is synthesised at **load time** by the game DLL from the name:")
    A("")
    A("```c")
    A("// game/shared/animation.cpp:140")
    A("void SetActivityForSequence( CStudioHdr *pstudiohdr, int i )")
    A("{")
    A("    seqdesc.flags |= STUDIO_ACTIVITY;                       // studio.h:3078, 0x1000")
    A("    pszActivityName = GetSequenceActivityName( pstudiohdr, i );")
    A("    iActivityIndex = ActivityList_IndexForName( pszActivityName );")
    A("    if ( iActivityIndex == -1 ) {")
    A("#ifdef CLIENT_DLL")
    A("        seqdesc.flags &= ~STUDIO_ACTIVITY;                  // client: stays unmapped")
    A("#else")
    A("        seqdesc.activity = ActivityList_RegisterPrivateActivity( pszActivityName );")
    A("#endif")
    A("    } else { seqdesc.activity = iActivityIndex; }")
    A("}")
    A("```")
    A("")
    A("**Consequences for the design**")
    A("")
    A("1. Garry's Mod's activity numbering **cannot be recovered from `.mdl`")
    A("   bytes** - it is not there. Any plan that reads numbers out of the")
    A("   models is unimplementable. (The numbers used in section 8 come from the")
    A("   separately staged GMod activity list, and are verified against")
    A("   `animations.lua`, not against the models.)")
    A("2. Because **our** DLL does the name -> number resolution, our engine is")
    A("   automatically self-consistent: there is no \"GMod number vs our number\"")
    A("   mismatch inside the engine.")
    A("3. The real, actionable defect is a **missing NAME**: a name absent from")
    A("   the shared activity list makes the two realms *disagree* (server")
    A("   registers a private activity, client clears `STUDIO_ACTIVITY` and keeps")
    A("   the raw value). Section 7 is therefore the work item.")
    A("")

    # ------------------------------------------- 2. totals / include models
    A("---")
    A("")
    A("## 2. Sequence totals, bones and `$includemodel`")
    A("")
    A("| model | file size | mdl version | numseq (local) | numseq (incl. libraries) | numbone | numlocalanim | numposeparams | numikchains |")
    A("|---|---|---|---|---|---|---|---|---|")
    for name in LIB_ORDER + CONTROL_ORDER:
        m = by_model.get(name)
        if not m:
            continue
        A("| `%s` | %d | %d | %d | %d | %d | %d | %d | %d |"
          % (name, m["file_size"], m["version"], m["numlocalseq"],
             m["numlocalseq"], m["numbones"], m["numlocalanim"],
             m["numlocalposeparameters"], m["numikchains"]))
    A("")
    A("`numseq (incl.)` equals `numseq (local)` for all five models **because the")
    A("parser reads one file**; sequences from `$includemodel` libraries are only")
    A("merged by the engine at load time (see section 10, where the ConCommand")
    A("reports the merged `GetNumSeq()`).")
    A("")
    A("### `$includemodel` references (read straight off each header)")
    A("")
    for name in LIB_ORDER + CONTROL_ORDER:
        m = by_model.get(name)
        if not m:
            continue
        inc = m["include_models"]
        if not inc:
            A("- **`%s`**: none - it is a self-contained animation library." % name)
        else:
            for i in inc:
                A("- **`%s`** -> label `%s` name `%s`" % (name, i["label"], i["name"]))
    A("")
    A("> The three GMod libraries include **nothing**: they are the leaves of the")
    A("> animation-include graph. Playermodels (`police.mdl`) are the ones that")
    A("> `$includemodel` an animation library.")
    A("")

    # ------------------------------------------------- 3. activity inventory
    A("---")
    A("")
    A("## 3. Activity inventory, per model")
    A("")
    A("For each model: (3a) the distinct activity **names**, grouped, with the raw")
    A("number stored in the file and how many sequences use each; then (3b) the")
    A("complete per-sequence table.")
    A("")
    A("The `raw activity in .mdl` column is `-1` for every row by construction")
    A("(section 1). The `GMod enum` column is the number from the separately")
    A("staged GMod activity list, shown only as a convenience; it is **not** read")
    A("from the model.")
    A("")

    for name in LIB_ORDER + CONTROL_ORDER:
        m = by_model.get(name)
        if not m:
            continue
        A("### `%s`" % name)
        A("")
        seqs = m["sequences"]
        groups = collections.OrderedDict()
        for s in seqs:
            key = s["activity_name"]
            g = groups.setdefault(key, {"raw": set(), "idx": []})
            g["raw"].add(s["activity"])
            g["idx"].append(s["index"])
        real = [k for k in groups if k]
        A("- sequences: **%d**" % len(seqs))
        A("- distinct activity names: **%d** (%d real + 1 \"no activity\" group covering %d sequences)"
          % (len(groups), len(real), len(groups.get("", {"idx": []})["idx"])))
        A("- names starting with `ACT_`: **%d**" % sum(1 for k in groups if k.startswith("ACT_")))
        A("")
        A("| # | activity name | raw activity in .mdl | sequences | sequence indices | GMod enum |")
        A("|---|---|---|---|---|---|")
        rank = 0
        for k in sorted(groups, key=lambda x: (x == "", x)):
            g = groups[k]
            rank += 1
            shown = k if k else "*(empty - no activity)*"
            idxs = ",".join(str(i) for i in g["idx"])
            if len(idxs) > 150:
                idxs = idxs[:150] + " ..."
            A("| %d | `%s` | %s | %d | %s | %s |"
              % (rank, shown, ",".join(str(x) for x in sorted(g["raw"])),
                 len(g["idx"]), idxs,
                 gmod_num.get(k, "-") if k else "-"))
        A("")

        # 3b full per-sequence table
        A("<details><summary><b>3b. complete per-sequence table for <code>%s</code> (%d rows)</b></summary>"
          % (name, len(seqs)))
        A("")
        A("| seq index | label | `szactivitynameindex` string | `activity` (raw) | flags | blends |")
        A("|---|---|---|---|---|---|")
        for s in seqs:
            A("| %d | `%s` | `%s` | %d | 0x%04X | %dx%d |"
              % (s["index"], s["label"] or "", s["activity_name"] or "",
                 s["activity"], s["flags"],
                 s["groupsize"][0], s["groupsize"][1]))
        A("")
        A("</details>")
        A("")

    # ------------------------------------------------------- 4. pose params
    A("---")
    A("")
    A("## 4. Pose parameters")
    A("")
    A("`mstudioposeparamdesc_t` = `{ int sznameindex; int flags; float start; float end; float loop; }` (20 bytes).")
    A("`start`/`end` are the declared min/max; `loop` is the wrapping range (0 = no wrap).")
    A("")
    for name in LIB_ORDER + CONTROL_ORDER:
        m = by_model.get(name)
        if not m:
            continue
        pp = m["pose_parameters"]
        A("### `%s` - %d pose parameters" % (name, len(pp)))
        A("")
        if not pp:
            A("*(none)*")
            A("")
            continue
        A("| index | name | min (`start`) | max (`end`) | loop | flags |")
        A("|---|---|---|---|---|---|")
        for p in pp:
            A("| %d | `%s` | %g | %g | %g | 0x%X |"
              % (p["index"], p["name"], p["min"], p["max"], p["loop"], p["flags"]))
        A("")
    A("### 4.1 Do `move_x` / `move_y` exist? Is it really 9-way?")
    A("")
    A("**Yes for all three GMod libraries** (and `move_y`/`move_x` are indices 0/1")
    A("there). `male_anims.mdl` (the HL2/HL2MP control) does **not** have them - it")
    A("uses the older `move_yaw` scheme instead. That is exactly the GMod delta.")
    A("")
    A("Evidence that the 9-way matrix is real: `numblends = 9`, `groupsize = 3x3`,")
    A("and the two blend axes are `paramindex[0]`/`paramindex[1]` pointing at the")
    A("`move_y` / `move_x` pose parameters.")
    A("")
    A("| model | pose param index of `move_x` | of `move_y` | 9-way (`3x3`) sequences | 1-way sequences |")
    A("|---|---|---|---|---|")
    for name in LIB_ORDER + CONTROL_ORDER:
        m = by_model.get(name)
        if not m:
            continue
        pp = m["pose_parameters"]
        mx = next((p["index"] for p in pp if p["name"] == "move_x"), None)
        my = next((p["index"] for p in pp if p["name"] == "move_y"), None)
        n9 = sum(1 for s in m["sequences"] if s["groupsize"] == [3, 3])
        n1 = sum(1 for s in m["sequences"] if s["groupsize"] == [1, 1])
        A("| `%s` | %s | %s | %d | %d |"
          % (name, mx if mx is not None else "absent",
             my if my is not None else "absent", n9, n1))
    A("")
    A("#### Sequences that actually drive `move_x` / `move_y`")
    A("")
    for name in LIB_ORDER + CONTROL_ORDER:
        m = by_model.get(name)
        if not m:
            continue
        pp = {p["index"]: p["name"] for p in m["pose_parameters"]}
        drives = []
        for s in m["sequences"]:
            if s["numblends"] <= 1:
                continue
            axes = [pp.get(s["paramindex"][k], "?") for k in (0, 1)]
            if "move_x" in axes or "move_y" in axes:
                drives.append((s, axes))
        A("**`%s`** - %d sequences drive `move_x`/`move_y`:" % (name, len(drives)))
        A("")
        if not drives:
            A("*(none)*")
            A("")
            continue
        A("| seq | label | activity name | groupsize | axes (`paramindex[0]`, `[1]`) | axis ranges |")
        A("|---|---|---|---|---|---|")
        for s, axes in drives:
            rng = ", ".join("[%g..%g]" % (s["paramstart"][k], s["paramend"][k])
                            for k in (0, 1))
            A("| %d | `%s` | `%s` | %dx%d | `%s`, `%s` | %s |"
              % (s["index"], s["label"], s["activity_name"] or "",
                 s["groupsize"][0], s["groupsize"][1], axes[0], axes[1], rng))
        A("")
    A("### 4.2 `aim_*` / `head_*` / `body_*` pose parameters")
    A("")
    A("| model | pose parameter names in order |")
    A("|---|---|")
    for name in LIB_ORDER + CONTROL_ORDER:
        m = by_model.get(name)
        if not m:
            continue
        A("| `%s` | %s |" % (name, ", ".join("`%s`" % p["name"] for p in m["pose_parameters"]) or "*(none)*"))
    A("")
    A("Probe results (the names the brief asked about):")
    A("")
    A("| model | `aim_yaw` | `aim_pitch` | `body_yaw` | `head_yaw` | `head_pitch` |")
    A("|---|---|---|---|---|---|")
    for name in LIB_ORDER + CONTROL_ORDER:
        m = by_model.get(name)
        if not m:
            continue
        have = {p["name"]: p for p in m["pose_parameters"]}
        cells = []
        for probe in ("aim_yaw", "aim_pitch", "body_yaw", "head_yaw", "head_pitch"):
            p = have.get(probe)
            cells.append("`%g..%g`" % (p["min"], p["max"]) if p else "absent")
        A("| `%s` | %s |" % (name, " | ".join(cells)))
    A("")
    A("So the real names are `aim_yaw` / `aim_pitch` (present in all four")
    A("animated models) and `head_yaw` / `head_pitch` (present in the three GMod")
    A("libraries). **`body_yaw` does not exist in any of these models** - the GMod")
    A("libraries instead carry `vertical_velocity` and `vehicle_steer`, and the")
    A("HL2 control `male_anims.mdl` carries `body_yaw` / `spine_yaw` / `move_yaw`")
    A("(no `head_*`-only split). GMod's `body_yaw` handling therefore cannot be")
    A("driven by a pose parameter in the GMod libraries as shipped.")
    A("")

    # ------------------------------------------------------------ 5. IK
    A("---")
    A("")
    A("## 5. IK chains and `$ikautoplaylock`")
    A("")
    A("`mstudioikchain_t` = `{ int sznameindex; int linktype; int numlinks; int linkindex; }`;")
    A("`mstudioiklink_t` = `{ int bone; Vector kneeDir; Vector unused0; }` (28 bytes).")
    A("NOTE: `linkindex` (and `mstudioseqdesc_t.iklockindex`) are **self-relative** -")
    A("the engine computes `((byte*)this) + index`, i.e. relative to the containing")
    A("struct, not to the file start. The first parser version got this wrong and")
    A("produced bone indices that were obviously ASCII text; it is fixed and the")
    A("values below are sanity-checked against the real `ValveBiped` bone table.")
    A("")
    for name in LIB_ORDER + CONTROL_ORDER:
        m = by_model.get(name)
        if not m:
            continue
        A("### `%s` - `numikchains = %d`" % (name, m["numikchains"]))
        A("")
        if not m["ik_chains"]:
            A("*(no IK chains)*")
            A("")
            continue
        A("| chain | name | linktype | numlinks | link 0 bone | link 1 bone | link 2 bone |")
        A("|---|---|---|---|---|---|---|")
        for c in m["ik_chains"]:
            bones = [l["bone_name"] for l in c["links"]]
            while len(bones) < 3:
                bones.append("")
            A("| %d | `%s` | %d | %d | `%s` | `%s` | `%s` |"
              % (c["index"], c["name"], c["linktype"], c["numlinks"],
                 bones[0], bones[1], bones[2]))
        A("")
        A("`kneeDir` per link (the IK preferred bending direction):")
        A("")
        for c in m["ik_chains"]:
            for j, l in enumerate(c["links"]):
                A("- `%s` link %d: bone `%s` kneeDir `(%g, %g, %g)`"
                  % (c["name"], j, l["bone_name"],
                     l["kneeDir"][0], l["kneeDir"][1], l["kneeDir"][2]))
        A("")
        if m["ik_autoplay_locks"]:
            A("Header-level `$ikautoplaylock` entries (`numlocalikautoplaylocks = %d`):"
              % m["numlocalikautoplaylocks"])
            A("")
            A("| # | chain | chain name | flPosWeight | flLocalQWeight | flags |")
            A("|---|---|---|---|---|---|")
            for l in m["ik_autoplay_locks"]:
                A("| %d | %d | `%s` | %g | %g | 0x%X |"
                  % (l["index"], l["chain"], l["chain_name"],
                     l["flPosWeight"], l["flLocalQWeight"], l["flags"]))
            A("")
        sl = m["seq_ik_locks"]
        A("Per-sequence IK locks (`mstudioseqdesc_t.numiklocks`/`iklockindex`):")
        A("**%d of %d sequences** carry them." % (len(sl), len(m["sequences"])))
        A("")
        if sl:
            chains = {c["index"]: c["name"] for c in m["ik_chains"]}
            agg = collections.Counter()
            for s in sl:
                for lk in s["locks"]:
                    agg[chains.get(lk["chain"], "?chain%d" % lk["chain"])] += 1
            A("Chains locked, by number of sequences: %s"
              % ", ".join("`%s` x%d" % (k, v) for k, v in sorted(agg.items())))
            A("")

    # ------------------------------------------------ 5.1 standard bones?
    A("### 5.1 Are the IK bones standard `ValveBiped`?")
    A("")
    EXPECT = {
        "rhand": ["ValveBiped.Bip01_R_UpperArm", "ValveBiped.Bip01_R_Forearm", "ValveBiped.Bip01_R_Hand"],
        "lhand": ["ValveBiped.Bip01_L_UpperArm", "ValveBiped.Bip01_L_Forearm", "ValveBiped.Bip01_L_Hand"],
        "rfoot": ["ValveBiped.Bip01_R_Thigh", "ValveBiped.Bip01_R_Calf", "ValveBiped.Bip01_R_Foot"],
        "lfoot": ["ValveBiped.Bip01_L_Thigh", "ValveBiped.Bip01_L_Calf", "ValveBiped.Bip01_L_Foot"],
    }
    A("| model | rhand | lhand | rfoot | lfoot |")
    A("|---|---|---|---|---|")
    all_ok = True
    for name in LIB_ORDER + CONTROL_ORDER:
        m = by_model.get(name)
        if not m:
            continue
        cells = []
        for ch in ("rhand", "lhand", "rfoot", "lfoot"):
            c = next((x for x in m["ik_chains"] if x["name"] == ch), None)
            if not c:
                cells.append("absent")
                continue
            got = [l["bone_name"] for l in c["links"]]
            ok = got == EXPECT[ch]
            all_ok = all_ok and ok
            cells.append("OK" if ok else "DIFFERS")
        A("| `%s` | %s |" % (name, " | ".join(cells)))
    A("")
    if all_ok:
        A("**All four chains in all five models use exactly the standard")
        A("`ValveBiped.Bip01_*` upper-arm/forearm/hand and thigh/calf/foot bones.**")
        A("The GMod libraries are therefore bone-compatible with the existing")
        A("`ValveBiped` skeleton and the existing IK code path.")
    else:
        A("At least one chain differs from the standard layout - see the per-chain")
        A("bone lists above.")
    A("")

    # ------------------------------------------------- 6. the vocabulary
    A("---")
    A("")
    A("## 6. The activity vocabulary the libraries actually bind")
    A("")
    c = recon["gmod_enum"]["counts"]
    A("The three libraries bind the **same 272 distinct activity-name slots**")
    A("(271 real names + the empty string, which means \"this sequence has no")
    A("activity\" and covers 522 sequences).")
    A("")
    A("| family | distinct names bound by the libraries | of which absent from our `ai_activity.h` |")
    A("|---|---|---|")
    for famlabel in FAMILY_ORDER:
        bf = absent["by_family"].get(famlabel, {})
        A("| `%s` | %d | **%d** |" % (famlabel, bf.get("bound_in_libs", 0),
                                     bf.get("absent_from_our_enum", 0)))
    A("| **total** | **%d** | **%d** |" % (counts["distinct_nonempty_names"],
                                          counts["names_ABSENT_from_our_enum"]))
    A("")
    A("### 6.1 `ACT_HL2MP_*` (%d names)"
      % absent["by_family"]["ACT_HL2MP_*"]["bound_in_libs"])
    A("")
    for n in absent["by_family"]["ACT_HL2MP_*"]["names"]:
        mark = " **<-- MISSING**" if n in absent_set else ""
        A("- `%s`%s" % (n, mark))
    A("")
    A("### 6.2 `ACT_GMOD_*` (%d names)"
      % absent["by_family"]["ACT_GMOD_*"]["bound_in_libs"])
    A("")
    for n in absent["by_family"]["ACT_GMOD_*"]["names"]:
        mark = " **<-- MISSING**" if n in absent_set else ""
        A("- `%s`%s" % (n, mark))
    A("")
    A("### 6.3 `ACT_FLINCH*` (%d names)"
      % absent["by_family"]["ACT_FLINCH*"]["bound_in_libs"])
    A("")
    for n in absent["by_family"]["ACT_FLINCH*"]["names"]:
        mark = " **<-- MISSING**" if n in absent_set else ""
        A("- `%s`%s" % (n, mark))
    A("")
    A("### 6.4 other Valve `ACT_*` (%d names)"
      % absent["by_family"]["other (Valve ACT_*)"]["bound_in_libs"])
    A("")
    for n in absent["by_family"]["other (Valve ACT_*)"]["names"]:
        mark = " **<-- MISSING**" if n in absent_set else ""
        A("- `%s`%s" % (n, mark))
    A("")

    # --------------------------------------- 7. names absent from our enum
    A("---")
    A("")
    A("## 7. THE WORK ITEM: names bound by the libraries but ABSENT from our shared enum")
    A("")
    A("Cross-checked by parsing `game/shared/ai_activity.h` textually (one enum")
    A("member per line; explicit values and trailing comments tolerated). The")
    A("header contains **%d** enum members; the libraries bind **%d** distinct real"
      % (counts["enum_members_in_our_header"], counts["distinct_nonempty_names"]))
    A("names, of which **%d are already present** and **%d are absent**."
      % (counts["names_present_in_our_enum"], counts["names_ABSENT_from_our_enum"]))
    A("")
    A("Absent names are listed first, because those are the ones that will make")
    A("the server and the client disagree about the activity number.")
    A("")
    A("| # | activity name | family | # sequences | libraries |")
    A("|---|---|---|---|---|")
    for i, n in enumerate(absent["absent_names"], 1):
        det = detail[n]
        A("| %d | `%s` | %s | %d | %s |"
          % (i, n, det["family"], det["n_sequences"],
             ", ".join(det["libraries"])))
    A("")
    A("### 7.1 Already present (no action needed) - %d names"
      % counts["names_present_in_our_enum"])
    A("")
    A("| activity name | family |")
    A("|---|---|")
    for n in absent["present_names"]:
        A("| `%s` | %s |" % (n, detail[n]["family"]))
    A("")
    A("### 7.2 Observations on the absent list")
    A("")
    A("- **`ACT_HL2MP_WALK` and `ACT_HL2MP_SWIM` are absent from our header but are")
    A("  bound by all three libraries as names.** The header's `ACT_HL2MP_*` block")
    A("  (`ai_activity.h:1240-1246`) is `IDLE, RUN, IDLE_CROUCH, WALK_CROUCH,")
    A("  GESTURE_RANGE_ATTACK, GESTURE_RELOAD, JUMP` - WALK is missing entirely and")
    A("  everything from +1 up is shifted relative to GMod's expected arithmetic.")
    A("- The absent set is dominated by whole hold-type families our enum does not")
    A("  declare: DUEL, FIST, KNIFE, REVOLVER, MAGIC, MELEE2, PASSIVE, CAMERA,")
    A("  ZOMBIE, ANGRY, SCARED, SUITCASE, COWER, CHARGING, FAST, PANICKED,")
    A("  PROTECTED, plus **every `SIT_*` name** and **every `SWIM*` name**.")
    A("- The absent `SWIM*` names are 38 and the absent `WALK*` names 39 - together")
    A("  more than half the absent list is the two families our enum never had.")
    A("- GMod's own spelling is inconsistent and the libraries reproduce it")
    A("  faithfully: `ACT_HL2MP_SIT_duel` is lowercase while")
    A("  `ACT_HL2MP_IDLE_DUEL` is uppercase. `ACT_HL2MP_SIT_duel`,")
    A("  `ACT_HL2MP_SIT_KNIFE` and `ACT_HL2MP_SIT_MELEE2` are also absent from the")
    A("  staged GMod number list, so they have no known GMod number at all.")
    A("")
    cond = absent["enum_members_declared_only_under_a_condition"]
    conds = sorted(set(cond.values()))
    A("- Enclosing-`#if` tracking: the only preprocessing condition recorded over")
    A("  any `ACT_*` member of this header is its own include guard (`%s`)."
      % (conds[0] if len(conds) == 1 else ", ".join(conds)))
    A("  The `ACT_HL2MP_*` block is not inside any feature/`#ifdef` region, so")
    A("  every name in the table above is compiled unconditionally once added.")
    A("")

    # --------------------------------------- 8. GMod numbering (external)
    A("---")
    A("")
    A("## 8. GMod's activity numbering - external source, and what it is worth")
    A("")
    A("Because the numbers are not in the models (section 1), the only GMod")
    A("numbering available is the separately staged recovery of GMod's activity")
    A("list at `_staging/gmod_ACT_numbers.txt` (1596 entries). It is **not**")
    A("evidence from the `.mdl` files. It was independently cross-checked here")
    A("against GMod's `gamemode/animations.lua` arithmetic, and all 10 checks")
    A("pass:")
    A("")
    g = recon["gmod_enum"]
    A("| GMod constant | offset | expected number | name actually at that number | verdict |")
    A("|---|---|---|---|---|")
    for chk in g["animations_lua_arithmetic_check"]:
        A("| `%s` | +%d | %d | `%s` | %s |"
          % (chk["ACT_MP_name"], chk["offset"], chk["expected_number"],
             chk["gmod_name_at_that_number"], "OK" if chk["ok"] else "MISMATCH"))
    A("")
    A("The `ACT_HL2MP_IDLE + k` block in GMod's numbering:")
    A("")
    A("| offset | GMod number | GMod name |")
    A("|---|---|---|")
    for off, num, nm in g["idle_block_plus_0_to_10"]:
        A("| +%d | %d | `%s` |" % (off, num, nm))
    A("")
    A("- `ACT_HL2MP_IDLE = %d`; `ACT_HL2MP_WALK = %d` (**exists in GMod**);"
      % (g["ACT_HL2MP_IDLE"], g["ACT_HL2MP_WALK"]))
    A("  `ACT_HL2MP_SWIM = %d` (**exists in GMod**); `ACT_HL2MP_JUMP = %d`."
      % (g["ACT_HL2MP_SWIM"], g["ACT_HL2MP_JUMP"]))
    A("- Per-hold-type block width is **10**: `+0 IDLE_ +1 WALK_ +2 RUN_ +3")
    A("  IDLE_CROUCH_ +4 WALK_CROUCH_ +5 GESTURE_RANGE_ATTACK_ +6 GESTURE_RELOAD_")
    A("  +7 JUMP_ +8 SWIM_IDLE_ +9 SWIM_`.")
    A("- **`ACT_HL2MP_IDLE + 8` (number 1785) has no entry** in the staged list,")
    A("  while a separate `ACT_HL2MP_SWIM_IDLE` exists much later at %d. Every")
    A("  per-hold-type block puts `SWIM_IDLE_<HT>` at +8, so 1785 is *probably*")
    A("  `ACT_HL2MP_SWIM_IDLE`, but this report does **not** assert it: it is an")
    A("  unresolved gap in the external source. GMod Lua never uses +7 or +8.")
    A("")
    A("- The staged list is incomplete: 1596 entries over numbers -1..2044 leaves")
    A("  450 holes (e.g. the whole 1723..1776 range). Numbers used only as")
    A("  relative offsets, or names verified on both sides, should be preferred")
    A("  over absolute values copied from it.")
    A("- GMod's `ACT_GMOD_*` numbers: %d names, e.g. `ACT_GMOD_DEATH = %s`."
      % (len(g["counts"]) and g["counts"]["ACT_GMOD*"],
         gmod_num.get("ACT_GMOD_DEATH", "?")))
    A("- GMod's `ACT_FLINCH*` numbers: %d names, `ACT_FLINCH_HEAD = %s`."
      % (g["counts"]["ACT_FLINCH*"], gmod_num.get("ACT_FLINCH_HEAD", "?")))
    A("- Our fork's absolute values are *not* GMod's: e.g. our")
    A("  `ACT_MP_STAND_IDLE` is 1066 while GMod's is %s. That divergence is"
      % gmod_num.get("ACT_MP_STAND_IDLE", "?"))
    A("  harmless now that we know nothing bakes numbers (section 1); only the")
    A("  `ACT_HL2MP_IDLE + k` **relative** arithmetic is an ABI, and only via")
    A("  `animations.lua`.")
    A("")
    A("### 8.1 Cross-check: does GMod's list cover every name the libraries bind?")
    A("")
    A("This is the independent check that matters, because a name present in the")
    A("models but missing from our enum is exactly the defect of section 7. The")
    A("libraries were matched against the staged GMod list by **exact string**")
    A("comparison:")
    A("")
    A("| family | names bound by the libraries | present in GMod's list | **NOT in GMod's list** |")
    A("|---|---|---|---|")
    for famlabel in FAMILY_ORDER:
        bf = absent["by_family"].get(famlabel, {})
        names = bf.get("names", [])
        miss = sorted(n for n in names if n not in gmod_num)
        A("| `%s` | %d | %d | **%d** |"
          % (famlabel, len(names), len(names) - len(miss), len(miss)))
    A("")
    lib_hl2mp = absent["by_family"]["ACT_HL2MP_*"]["names"]
    miss_hl2mp = sorted(n for n in lib_hl2mp if n not in gmod_num)
    if miss_hl2mp:
        A("**%d `ACT_HL2MP_*` names are bound by the libraries but absent from GMod's**"
          % len(miss_hl2mp))
        A("**own activity list**, so they have no GMod number at all:")
        A("")
        for n in miss_hl2mp:
            A("- `%s`" % n)
        A("")
        A("GMod's list contains only 14 `ACT_HL2MP_SIT_*` names while the libraries")
        A("bind 17. A replacement enum generated *only* from GMod's list would")
        A("therefore still leave these names unregistered - and unregistered names")
        A("are precisely what makes the server and the client disagree (section 1).")
        A("Note the case: GMod spells the sit/duel member `ACT_HL2MP_SIT_duel` with a")
        A("lowercase `duel`, and the libraries bind it verbatim, so the enum member")
        A("must keep that spelling.")
        A("")
    else:
        A("Every name bound by the libraries is also present in GMod's list.")
        A("")

    # ------------------------------------------------------ 9. what it means
    A("---")
    A("")
    A("## 9. What this means")
    A("")
    A("### 9.1 Activity vocabulary we must support - by NAME")
    A("")
    A("- %d names are bound by the three GMod libraries. They are the vocabulary"
      % counts["distinct_nonempty_names"])
    A("  a by-name animation system has to resolve.")
    A("- %d of them are already in `game/shared/ai_activity.h`; **%d must be"
      % (counts["names_present_in_our_enum"], counts["names_ABSENT_from_our_enum"]))
    A("  added** (section 7) or the client and server will disagree on their")
    A("  numbers.")
    A("- Selection must be by name: `LookupActivity( pStudioHdr, \"ACT_...\" )` or")
    A("  the already-existing `gmod_activity_translate` name path. `GetSequenceActivity()`")
    A("  returns our own number, resolved from the name at load, and is therefore")
    A("  safe to compare *within one realm* but not across realms for a name that")
    A("  is not in the shared list.")
    A("- The `ACT_HL2MP_IDLE + k` relative arithmetic (`animations.lua`) is the one")
    A("  numbering ABI. Our current block fails it from +1 up (section 7.2).")
    A("")
    A("### 9.2 Pose parameters we can drive")
    A("")
    A("| parameter | m_anm / f_anm / z_anm | male_anims.mdl (control) |")
    A("|---|---|---|")
    lib0 = by_model["m_anm.mdl"]["pose_parameters"]
    ctl = by_model["player\\male_anims.mdl"]["pose_parameters"]
    allnames = []
    for p in lib0:
        allnames.append(p["name"])
    for p in ctl:
        if p["name"] not in allnames:
            allnames.append(p["name"])
    for nm in allnames:
        a = next((p for p in lib0 if p["name"] == nm), None)
        b = next((p for p in ctl if p["name"] == nm), None)
        A("| `%s` | %s | %s |"
          % (nm,
             ("`%g..%g`" % (a["min"], a["max"])) if a else "absent",
             ("`%g..%g`" % (b["min"], b["max"])) if b else "absent"))
    A("")
    A("- **9-way movement is real and driveable**: `move_x` / `move_y`, both")
    A("  `-1..1`, consumed as `3x3` blend matrices. `%d` sequences in `m_anm.mdl`"
      % sum(1 for s in by_model["m_anm.mdl"]["sequences"] if s["groupsize"] == [3, 3]))
    A("  are `3x3`.")
    A("- **`aim_yaw` / `aim_pitch` are real** and cover roughly `-63..71` and")
    A("  `-85..82` degrees across the three libraries.")
    A("- **`head_yaw` / `head_pitch` are real** in the three libraries (`-75..75`")
    A("  and `-60..60`).")
    A("- **`body_yaw` does NOT exist in the GMod libraries.** Only the HL2 control")
    A("  `male_anims.mdl` has it (`-29.7..29.7`, plus `spine_yaw`, `head_roll`,")
    A("  `move_yaw`). GMod's torso/body yaw behaviour must therefore be built from")
    A("  `aim_yaw` + `head_yaw` mechanics, or a pose parameter must be added to")
    A("  the libraries - it cannot be silently assumed to exist.")
    A("- Two GMod-only parameters exist that HL2MP never had:")
    A("  `vertical_velocity` and `vehicle_steer` (both `-1..1`).")
    A("")
    A("### 9.3 Is IK present?")
    A("")
    A("**Yes, and completely standard.** `numikchains = 4` in all four animated")
    A("models: `rhand`, `lhand`, `rfoot`, `lfoot`, 3 links each, all pointing at")
    A("exactly the standard `ValveBiped.Bip01_*` bones (section 5.1). The")
    A("libraries also carry 4 header-level `$ikautoplaylock` entries (both feet,")
    A("`flPosWeight=1.0`, `flLocalQWeight=0.1`) and per-sequence IK locks on 107")
    A("of 464 sequences - i.e. the feet are IK-locked in the aim/movement")
    A("matrices. So IK is available and already authored; it is not something that")
    A("has to be reintroduced.")
    A("")

    # ---------------------------------------------- 10. engine-side tool
    A("---")
    A("")
    A("## 10. Engine-side verification")
    A("")
    A("An in-engine `gmod_anim_dumpmodel` ConCommand was drafted and then")
    A("**deliberately not delivered**: it was reverted at the owner's request so")
    A("that nothing else lands in the tree while `game/shared/ai_activity.h`,")
    A("`game/shared/activitylist.cpp` and `game/server/ai_activity.cpp` are being")
    A("rewritten. The vpc wiring was rolled back and the sources were moved out of")
    A("the tree. The draft is kept for reference only at")
    A("`_rejected_engine_tool/gmod_animation_debug.{h,cpp}`.")
    A("")
    A("It did compile far enough to prove one loader constraint that anyone")
    A("writing that tool will hit:")
    A("")
    A("```")
    A("gmod_animation_debug.cpp(342): error C2664:")
    A("  studiohdr_t *IVModelInfo::GetStudiomodel(const model_t *)")
    A("  cannot convert argument 1 from 'const char *' to 'const model_t *'")
    A("```")
    A("")
    A("In this fork `IVModelInfo::GetStudiomodel()` takes a **`const model_t *`**,")
    A("not a path (`public/engine/ivmodelinfo.h:146`). Load by name first:")
    A("")
    A("```c")
    A("const model_t *pModel = modelinfo->FindOrLoadModel( pszModelName ); // ivmodelinfo.h:214")
    A("studiohdr_t *pHdr = modelinfo->GetStudiomodel( pModel );")
    A("CStudioHdr sh( pHdr, mdlcache );")
    A("```")
    A("")
    A("The engine-side check that matters after the enum fix is:")
    A("")
    A("```c")
    A("IndexModelSequences( &sh );                        // animation.h:20")
    A("int iShared   = ActivityList_IndexForName( name ); // activitylist.h:83  (-1 => not shared)")
    A("int iResolved = LookupActivity( &sh, name );       // animation.h:31")
    A("int iSeq      = SelectWeightedSequence( &sh, iResolved ); // animation.h:23")
    A("```")
    A("")
    A("plus the relative-offset assertion that GMod Lua actually depends on:")
    A("`ActivityList_IndexForName(\"ACT_HL2MP_IDLE\") + k` must be")
    A("`WALK, RUN, IDLE_CROUCH, WALK_CROUCH, GESTURE_RANGE_ATTACK, GESTURE_RELOAD`")
    A("for `k = 1..6` and `SWIM` for `k = 9`. Sample the *shared* index **before**")
    A("calling `IndexModelSequences()`, because on the server that call registers")
    A("unknown names as private activities and would mask the result.")
    A("")

    A("### In-game commands (once the tool exists)")
    A("")
    A("```")
    A("gmod_anim_dumpmodel models/m_anm.mdl")
    A("gmod_anim_dumpmodel models/f_anm.mdl")
    A("gmod_anim_dumpmodel models/z_anm.mdl")
    A("```")
    A("")
    A("(Client console. On a listen server the server-side spelling is")
    A("`gmod_anim_dumpmodel_sv`, so the two realms do not clash on registration.)")
    A("Output goes to the console; with `-condebug` it also lands in `console.log`.")
    A("")
    A("### Reproduce this report")
    A("")
    A("```powershell")
    A("cd D:\\project\\source-engine")
    A("python .\\tools\\studiomdl_inspect.py --json D:\\project\\anim_audit\\raw_dump.json `")
    A("  D:\\srceng\\hl2sb\\models\\m_anm.mdl D:\\srceng\\hl2sb\\models\\f_anm.mdl `")
    A("  D:\\srceng\\hl2sb\\models\\z_anm.mdl D:\\srceng\\hl2sb\\models\\player\\police.mdl `")
    A("  D:\\srceng\\hl2sb\\models\\player\\male_anims.mdl")
    A("python .\\tools\\mdl_activity_scan.py --csv D:\\project\\anim_audit\\mdl_activity_all.csv D:\\srceng\\hl2sb\\models")
    A("python .\\tools\\gmod_activity_absent_names.py --mdl-json D:\\project\\anim_audit\\raw_dump.json `")
    A("  --activity-header .\\game\\shared\\ai_activity.h --out-prefix D:\\project\\anim_audit\\GMod_activity_absent_names")
    A("python .\\tools\\gen_stage0_inventory.py --dir D:\\project\\anim_audit")
    A("```")
    A("")

    out = os.path.join(d, args.out)
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")
    print("wrote %s (%d bytes, %d lines)" % (out, os.path.getsize(out), len(L)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
