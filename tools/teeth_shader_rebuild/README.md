# teeth_shader_rebuild

Offline rebuild pipeline for the `teeth_*` vertex shader `.vcs` containers.

Why: the shipped `hl2_misc_dir.vpk` teeth vertex shaders let garbage mouth
vertices (positions far outside anything a mouth can reach) through to the
screen, painting giant unlit black triangles anchored to citizen models.
The fix lives in the vertex shader sources
(`materialsystem/stdshaders/teeth_vs20.fxc`, `teeth_bump_vs20.fxc`,
`teeth_flashlight_vs20.fxc` - a range clamp that collapses those vertices
to one shared clip position, aligned with the upstream community patch for
source-sdk-2013 issue 683).  The engine loads compiled `.vcs` bytecode from
the search paths, so patched sources only take effect once the containers
are rebuilt and dropped into `hl2sb/shaders/fxc/` (mod root wins over the
hl2 vpk).

Scripts (development-order; paths inside are machine-specific, adjust
before reuse):

- `prep_src.py`   stages the .fxc sources plus their HLSL headers into a
                  scratch dir (fxc.exe needs the includes next to it)
- `patch_fxc.py`  applies the clamp to the three sources; the in-tree
                  `dx9sdk/utilities/fxc.exe` rejects early returns from
                  conditionals (X3500), so the guard is written as
                  if/else with a single return
- `vcs.py`        reader/writer for the .vcs v6 container (format
                  cross-checked against
                  `public/materialsystem/shader_vcs_version.h` and
                  `materialsystem/shaderapidx9/vertexshaderdx8.cpp`)
- `build_vcs.py`  full pipeline: parse the `// STATIC:`/`// DYNAMIC:`/
                  `// SKIP:` combo headers (mirrors fxc_prep.pl and the
                  cfgprocessor combo-id encoding), run one fxc.exe
                  invocation per combo
                  (`dx9sdk/utilities/fxc.exe`, see
                  `utils/shadercompile/d3dxfxc.cpp` for the flag recipe),
                  assemble the containers as uncompressed blocks
- `verify.py`     structural + bytecode verification of the rebuilt
                  containers against pristine originals

Validated: unpatched rebuild of the vs_2_0 targets matches the shipped
bytecode for every combo except the CTAB compiler-version string; patched
rebuilds carry the clamp in all 624 combos across the six outputs
(`teeth_{,bump_,flashlight_}vs{20,30}.vcs`).
