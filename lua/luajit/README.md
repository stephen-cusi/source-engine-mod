# vendor: LuaJIT 2.1 (GLua dialect)

LuaJIT 2.1.0-beta3 rolling lineage, vendored for the HL2SB `lua_shared`
target (replacing the previous Lua 5.4.6 core on this branch).

## Provenance

- Base: LuaJIT 2.1 rolling (upstream https://github.com/LuaJIT/LuaJIT,
  MIT license, see COPYRIGHT).
- Dialect patches carried in-tree (same set the reference engine ships):
  - `lj_lex.c` / `lj_parse.c`: `&&` `||` `!` `!=` operators, `continue`
    statement, `//` and `/* */` comments.
  - `lib_table.c`: `table.insert` returns the inserted position.
  - `lj_api.c`: `luaL_newmetatable_type` (MetaID slot for the type
    registry), `lua_resume_real`.
  - `lib_gmod.c`: binary-module platform layer (ILuaBase vtable bridge,
    GMOD_LoadBinaryModule / GMOD_PooledString contract).
- The fork previously maintained a Lua 5.4.6 core with equivalent dialect
  patches bolted on (see `lua/src` history on the parent branch). This
  branch swaps the core so dialect semantics are native instead of
  simulated, and the 5.1 C-API export shims largely retire.

## Layout

- `src/` - LuaJIT core + standard libraries + `host/` (minilua and
  buildvm build-time host tools) + `jit/*.lua` runtime bit library.
- `src/vm_*.dasc` - DynASM per-architecture assembler sources. Only
  `vm_x64.dasc` (win64/linux64, GC64) and `vm_arm64.dasc` (android64)
  are consumed by this repo's build; the others are kept for upstream
  fidelity.
- `dynasm/` - DynASM preprocessor used at build time by minilua to
  generate `buildvm_arch.h` per target architecture.
- Build integration lives in `lua/wscript` (host tools are built by waf,
  then run to emit per-target `lj_vm` objects and definition headers).

## Build constraints

- win64/linux64 link GC64 by default (required: consumers reach state
  fields through pointer-table indirection; non-GC64 builds crash the
  host).
- android64 (arm64) runs the default W^X mcode protection path
  (RW<->RX mprotect twiddling, never RWX) - Android 10+ rejects
  writable-executable mappings. Do not define LUAJIT_UNPROTECT_MCODE.
- Files are plain ASCII; the repo-wide /utf-8 flag (win32) does not
  change that requirement.
