#!/usr/bin/env python3
"""Offline preview of the HL2SB SMenu dynamic content list.

SMenu no longer reads addons/menu/entitylist.txt *as its content*: it enumerates
the client class dictionary and the server's entity factory dictionary at
runtime, then merges GMod-style Lua registries.  This script reconstructs those
sources from the tree so the result can be inspected without launching the game:

  * CLIENT class dictionary (game/client/classmap.cpp -> CClassMap):
      LINK_ENTITY_TO_CLASS( a, b )   - game/shared/predictable_entity.h:146
      STUB_WEAPON_CLASS( a, b, c )   - game/client/c_weapon__stubs.h:25
      + the Lua loaders' runtime registrations (RegisterScriptedWeapon /
        RegisterScriptedEntity -> the "scripted" flag)
  * SERVER entity factory dictionary (game/server/util.cpp ->
    CEntityFactoryDictionary), which is what the server publishes over the
    "SMenuEntityList" string table.  When the stored dump
    <game>/addons/menu/entitylist.txt exists it is used verbatim - it was
    produced from that very dictionary by `dumpentitytofile`; otherwise pass
    --no-dump to leave the server side out.
  * Lua registries: list.Set("SpawnableEntities", ...) /
    list.Set("Weapon", ...) - validated against the two engine sources, exactly
    like SMenu_MergeLuaList() does.

The hide list and the category rules below are a transcription of
game/client/menu/sm_menu_list.cpp (SMenu_IsHiddenClass / SMenu_Classify); keep
the two in sync.

Usage:
    python tools/smenu_class_preview.py [--repo D:\\project\\source-engine]
                                        [--game D:\\srceng\\hl2sb]
                                        [--show vehicles]
"""

import argparse
import os
import re
import sys

VPCS = ["client_base.vpc", "client_hl2mp.vpc", "client_lua.vpc"]

HIDE_EXACT = ["worldspawn", "player", "predicted_viewmodel", "viewmodel",
              "soundent", "spotlight_end", "localname", "entityname",
              "reserved_spot", "world_items", "sky_camera", "bodyque",
              "entity_blocker", "water_lod_control", "hammer_updateignorelist",
              "te_tester", "lookdoorthinker", "scene_manager"]
HIDE_PREFIX = ["_", "base", "ai_", "logic_", "math_", "path_", "filter_", "func_",
               "trigger_", "info_", "point_", "env_", "phys_", "game_", "team_",
               "vgui_", "move_", "keyframe_", "rope_", "script_", "scripted_",
               "commentary_", "player_", "tanktrain_", "instanced_", "material_",
               "handle_", "test_", "hammer_", "cycler", "sky_", "water_",
               "event_queue_"]

LUA_SCRIPTS = ["shared.lua", "init.lua", "cl_init.lua"]

RE_DUMP_ENTITY = re.compile(r'"entity"\s+"([^"]+)"')


def expand(value, macros, base):
    out = value
    for _ in range(6):
        new = re.sub(r"\$(\w+)",
                     lambda m: macros.get(m.group(1).upper(), m.group(0)),
                     out)
        if new == out:
            break
        out = new
    out = out.replace("/", os.sep).replace("\\", os.sep)
    if not os.path.isabs(out):
        out = os.path.normpath(os.path.join(base, out))
    return out


def parse_vpc(path, macros, seen, files):
    path = os.path.normpath(path)
    if path.lower() in seen or not os.path.isfile(path):
        return
    seen.add(path.lower())

    base = os.path.dirname(path)
    local = dict(macros)
    local.setdefault("SRCDIR", os.path.normpath(os.path.join(base, "..", "..")))

    with open(path, "r", errors="replace") as handle:
        text = handle.read()

    for line in text.splitlines():
        m = re.match(r'\s*\$Macro\s+(\S+)\s+"([^"]*)"', line, re.I)
        if m:
            local[m.group(1).upper()] = expand(m.group(2), local, base)

    for line in text.splitlines():
        stripped = line.strip()
        m = re.match(r'\$Include\s+"([^"]+)"', stripped, re.I)
        if m:
            parse_vpc(expand(m.group(1), local, base), local, seen, files)
            continue
        m = re.match(r'(-\s*)?\$File\s+"([^"]+)"', stripped, re.I)
        if m and not m.group(1):
            files.add(expand(m.group(2), local, base))


def collect_sources(repo):
    files = set()
    seen = set()
    for vpc in VPCS:
        parse_vpc(os.path.join(repo, "game", "client", vpc), {}, seen, files)
    return {f for f in files
            if f.lower().endswith((".cpp", ".h"))
            and os.sep + "game" + os.sep in f.lower()
            and os.sep + "server" + os.sep not in f.lower()}


RE_LINK = re.compile(r'\bLINK_ENTITY_TO_CLASS\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)')
RE_STUB = re.compile(r'\bSTUB_WEAPON_CLASS\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*,')


def collect_classes(repo):
    classes = {}          # class name -> (cpp name, scripted)
    for source in sorted(collect_sources(repo)):
        if not os.path.isfile(source):
            continue        # vpc lists entries for trees this fork does not have
        with open(source, "r", errors="replace") as handle:
            text = handle.read()
        for m in RE_LINK.finditer(text):
            classes.setdefault(m.group(1).lower(), ("C_" + m.group(2), False))
        for m in RE_STUB.finditer(text):
            classes.setdefault(m.group(1).lower(), ("C_" + m.group(2), False))
    return classes


def collect_lua(game):
    lua = {}

    weapons = os.path.join(game, "lua", "weapons")
    if os.path.isdir(weapons):
        for name in os.listdir(weapons):
            full = os.path.join(weapons, name)
            if os.path.isdir(full):
                if any(os.path.isfile(os.path.join(full, s)) for s in LUA_SCRIPTS):
                    lua[name.lower()] = ("CHL2MPScriptedWeapon", True)
            elif name.lower().endswith(".lua"):
                lua[os.path.splitext(name)[0].lower()] = ("CHL2MPScriptedWeapon", True)

    entities = os.path.join(game, "lua", "entities")
    if os.path.isdir(entities):
        for name in os.listdir(entities):
            full = os.path.join(entities, name)
            if os.path.isdir(full):
                if any(os.path.isfile(os.path.join(full, s)) for s in LUA_SCRIPTS):
                    lua[name.lower()] = ("CBaseScripted", True)
            elif name.lower().endswith(".lua"):
                lua[os.path.splitext(name)[0].lower()] = ("CBaseScripted", True)

    return lua


def is_hidden(name, scripted=False):
    # Never hide Lua-registered content: an addon's class may start with a
    # prefix that is junk for a stock class (trigger_scripted is a SENT).
    if scripted:
        return False
    if name in HIDE_EXACT:
        return True
    return any(name.startswith(p) for p in HIDE_PREFIX)


def classify(name, cpp, scripted):
    scripted_weapon = scripted and "weapon" in cpp.lower()
    prefix_weapon = any(name.startswith(p) for p in ("weapon_", "item_", "ammo_", "gmod_"))
    if scripted_weapon or prefix_weapon or "weapon" in cpp.lower():
        return "Weapons/Lua SWEPs" if scripted else "Weapons/Stock"
    if scripted:
        return "Entities/Lua entities"
    if name.startswith(("npc_", "monster_")) or "npc" in cpp.lower():
        return "NPCs"
    if name.startswith(("prop_vehicle_", "vehicle_")) or "vehicle" in cpp.lower():
        return "Vehicles"
    if name.startswith(("prop_", "physbox")) or "prop" in cpp.lower() or "physbox" in cpp.lower():
        return "Props"
    return "Entities/Stock entities"


def collect_server_dump(game):
    """The server's entity factory dictionary, as dumped by dumpentitytofile."""
    dump = os.path.join(game, "addons", "menu", "entitylist.txt")
    if not os.path.isfile(dump):
        return {}

    with open(dump, "r", errors="replace") as handle:
        return {name.lower(): ("", False) for name in RE_DUMP_ENTITY.findall(handle.read())}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=r"D:\project\source-engine")
    ap.add_argument("--game", default=r"D:\srceng\hl2sb")
    ap.add_argument("--no-dump", action="store_true",
                    help="leave the server entity dictionary out of the preview")
    ap.add_argument("--show", default="", help="print the entries of categories containing this text")
    args = ap.parse_args()

    client = collect_classes(args.repo)
    server = {} if args.no_dump else collect_server_dump(args.game)
    lua = collect_lua(args.game)

    # SMenu_BuildEntries(): client class map first (it knows the Lua flag), then
    # the server dictionary, then the Lua registries (validated).
    classes = dict(client)
    n_server_added = 0
    for name, entry in server.items():
        if name not in classes:
            classes[name] = entry
            n_server_added += 1
    for name, entry in lua.items():
        classes[name] = entry

    buckets = {}
    hidden = []
    for name in sorted(classes):
        cpp, scripted = classes[name]
        if is_hidden(name, scripted):
            hidden.append(name)
            continue
        buckets.setdefault(classify(name, cpp, scripted), []).append(name)

    print("client class map entries : %d" % len(client))
    print("server factory entries   : %d (%d of them new to the client)"
          % (len(server), n_server_added))
    print("lua registrations        : %d" % len(lua))
    print("union                    : %d entries (%d hidden by the SMenu filter)"
          % (len(classes), len(hidden)))
    print("")
    total = 0
    for bucket in sorted(buckets):
        print("  %-24s %4d" % (bucket, len(buckets[bucket])))
        total += len(buckets[bucket])
    print("  %-24s %4d" % ("LISTED", total))

    if args.show:
        needle = args.show.lower()
        for bucket in sorted(buckets):
            if needle in bucket.lower():
                print("\n[%s]\n  %s" % (bucket, ", ".join(buckets[bucket])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
