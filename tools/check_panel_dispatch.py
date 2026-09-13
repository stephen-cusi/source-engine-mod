#!/usr/bin/env python3
"""Check every Lua panel-method dispatch for a mismatched argument count.

Why this exists
---------------
The scripted controls dispatch C++ callbacks into Lua with

    BEGIN_LUA_CALL_PANEL_METHOD( "OnFoo" );     // pushes the receiver itself
        lua_push<whatever>( ... );              // each extra arg the Lua method takes
    END_LUA_CALL_PANEL_METHOD( nArgs, nresults );

`nArgs` is the number of EXTRA values pushed after the receiver
(the macro starts its own `args` counter at 1 for `this`).  If it does not match
the pushes, `lua_pcall()` is handed a function slot that is off by the
difference, so it calls the WRONG VALUE -- the real panel for an under-count
("attempt to call a Panel value") or whatever sits below the function for an
over-count -- and the Lua method either never runs or runs with the wrong
arguments.

Both directions were live in this tree:

  * game/client/lua/scripted_controls/lPanel.cpp  LPanel::OnChildAdded
    pushed the child but declared 0 -> DDragBase/DListLayout's
    `child:Dock( TOP )` never ran, so a list layout never docked its rows and
    kept the 64x24 Panel default height.
  * game/client/lua/scripted_controls/lFrame.cpp  eight handlers declared an
    argument they never pushed, so every scripted Frame handler received the
    traceback handler in place of its real argument.

`luasrc_pcall()` only Warnings the error, so both were silent apart from a
console traceback.  Run this after touching any dispatch site:

    python tools/check_panel_dispatch.py            # from the repo root
    python tools/check_panel_dispatch.py <dir> ...  # or explicit directories

Exits non-zero when a mismatch is found, so it can gate a build.
"""

import os
import re
import sys

DEFAULT_ROOTS = (
    "game/client/lua/scripted_controls",
    "public/lua/vgui_controls",
)

BEGIN = re.compile(r"(BEGIN_LUA_CALL_PANEL_METHOD|LUA_CALL_PANEL_METHOD_BEGIN)\s*\(")
END = re.compile(r"(END_LUA_CALL_PANEL_METHOD|LUA_CALL_PANEL_METHOD_END)\s*\(\s*(\d+)")


def check_file(path):
    mismatches = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")

    i = 0
    while i < len(lines):
        if BEGIN.search(lines[i]):
            pushes = 0
            j = i + 1
            while j < len(lines) and not END.search(lines[j]):
                if "lua_push" in lines[j]:
                    pushes += 1
                j += 1
            if j < len(lines):
                declared = int(END.search(lines[j]).group(2))
                if declared != pushes:
                    mismatches.append((i + 1, lines[i].strip(), pushes, declared))
                i = j
        i += 1
    return mismatches


def main(argv):
    roots = argv[1:] or list(DEFAULT_ROOTS)
    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

    checked = 0
    failed = 0
    for root in roots:
        base = root if os.path.isabs(root) else os.path.join(root_dir, root)
        for dirpath, _dirnames, filenames in os.walk(base):
            for name in filenames:
                if not name.endswith((".cpp", ".h")):
                    continue
                path = os.path.join(dirpath, name)
                checked += 1
                for line_no, text, pushes, declared in check_file(path):
                    failed += 1
                    rel = os.path.relpath(path, root_dir)
                    print(
                        "MISMATCH %s:%d  pushed=%d nArgs=%d  | %s"
                        % (rel, line_no, pushes, declared, text)
                    )

    print("checked %d file(s), %d mismatch(es)" % (checked, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
