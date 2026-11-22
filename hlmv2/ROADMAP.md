# HLMV2 Roadmap

Goal: bring the Qt port of the original Half-Life Model Viewer (`utils/hlmv`)
to feature and behaviour parity, in small verified steps. Checked items are
implemented and confirmed by a check or a direct workflow test; partial support
stays explicitly unchecked.

## Approximate Original-HLMV Coverage
Roughly 40% by engineering judgment, not a measured feature count. Model
loading, the orbit camera, skeletal and CPU skinning, sequence playback, bodygroup
and skin selection, VMT/VTF texture resolution, attachments, and the Recent
Models list all work. Most of the remaining gap is in the Control Panel: several
pages are placeholders, and the original debug overlays (hit boxes, normals,
physics) are missing. Reassess after each item below is verified.

## Verified Working
- [x] Load a model via File > Load Model, with recent-models persistence.
- [x] Orbit camera, wheel zoom, pan; Center View.
- [x] Skeletal playback with the engine's bone setup, sequence list, play/pause,
  speed, and Force To Frame slider (playback speed value is not yet synced, see bugs).
- [x] Bodygroup selection (one combo per bodypart) and skin family selection
  (see bug B1: skin changes do not refresh the view).
- [x] VMT -> VTF texture resolution with per-texture status tooltips.
- [x] Attachments list and in-viewport crosshairs.
- [x] Merged weapon (hands) model load, bone-name matched, with unload-all.
- [x] Wireframe, Bones, Attachments, Ground toggles, with Ctrl-W/O/A/G shortcuts.
- [x] Screenshot (PNG). Original writes TGA; see Parity Items.
- [x] Built-in English plus external `hlmvlang/hlmv_<locale>.qm` catalogs, with a
  View > Language menu (applies after restart).
- [x] `--selftest` headless model pipeline.

## Parity Items (missing from the original)

### High
- [ ] Hit Boxes overlay (Ctrl+H, Render page checkbox) and hitbox drawing. The
  original's most-used debug view; no draw path exists yet.
- [ ] Bones page hitbox editing: hitbox set add/rename/delete, hitbox list
  highlight, group/name/origin/size editing, and Autogenerate Hitboxes.
- [ ] Pose Parameter control on the Sequence page: a per-parameter combo and
  slider wired to the model's pose parameters. Blended animations currently show
  the wrong pose.
- [ ] Flex page: flex slider list and Reset/Random/Zero/One. The placeholder says
  this needs the engine material system; the flex data is in the MDL header and
  should be readable. Verify before implementing.

### Medium
- [ ] Render-mode values BoneWeights, BadVertexData, UV Chart (currently disabled).
- [ ] Normal Mapping, Specular, Overbrightening, Normals, Tangents (currently
  disabled or unhandled).
- [ ] Movement, Background (incl. background texture load), Shadow, Software Skin.
- [ ] Physics Model overlay and the physics-bone highlight selector.
- [ ] Physics page editing: mass, constraint axes, friction, min/max, test,
  mass bias/inertia/damping, Generate QC. Physics mesh drawing (`physmesh.cpp`).
- [ ] Enable IK and Head Turn; IKRule page (placeholder says it needs the engine
  IK context; IK chain counts are already read, verify).
- [ ] Options > Background, Ground, Light and Ambient colour pickers (originally
  enabled; currently disabled).
- [ ] View > Show Activities and Show hidden sequence filters (currently disabled).
- [ ] Unload Weapon per-slot list (1-12); only Unload All works.
- [ ] Controller slider, LOD selection, Auto LOD, and LOD metric.
- [ ] Per-model view settings save/load (original `viewersettings.cpp`).
- [ ] Attachments window: translate/rotate editing, rebind to a bone, update QC string.
- [ ] Viewmodel Mode: original camera origin at 54 degrees; hlmv2 uses an orbit
  camera with presets.
- [ ] Events page (placeholder; event list is in the MDL sequence descriptions,
  verify before implementing).
- [ ] Blend Sequence Changes, Blend Now, Blend Time; Animate weapons; 5 animation
  layers (currently disabled).

### Low
- [ ] Link / Unlink HLMV sync.
- [ ] Screenshot as TGA to match original output.
- [ ] Shift-drag, Shift-right-drag and Ctrl-drag navigation bindings from the
  original About text.
- [ ] Illum. Position, Play Sounds, Show Origin Axis with axis length slider,
  FOV input.
- [ ] Help > Goto Homepage points at the Valve wiki; original points at the
  swissquake page. Decide which is intended.
- [ ] Steam-variant Load Model menu entries (not applicable outside the
  Steam-enabled build).

## Known Bugs (from code review, to fix)
Reviewed by reading the source, not by running the program. Items marked
"confirmed" were checked against the code path; "unverified" need a repro.

Crashes and out-of-bounds reads on malformed model input (highest priority):
- [x] B1 (confirmed) [fixed, selftest]: `studiodata.cpp` VTX parsing does not bound-check
  pBodypart/pModel/pLod/pMesh/pGroup/pStrip offsets or counts. Vertex indices
  are used without comparison to group vertex count.
- [x] B2 (confirmed) [fixed, selftest]: VVD parsing does `nNumVertexes * sizeof(VvdVertex_t)` in
  `int`, which overflows for very large counts and bypasses the length check.
  Fixup table and source vertex IDs are not range-checked.
- [x] B3 (confirmed) [fixed, selftest]: `.mdl` string offsets (cdtextures, sznameindex) and
  bodypart/model counts are not checked against file size. `numbodyparts` and
  `nummodels` are unbounded.
- [x] B4 (confirmed) [fixed, selftest]: include-model loading (`LoadIncludeModels`) accepts any file
  with a matching "IDST" magic and length >= 64, then reads it as a full
  `studiohdr_t`. Short files read past their end.
- [x] B5 (unverified) [fixed, desktop check pending]: PopulateBones: a bone whose parent index equals its own
  index adds the item as its own child, which may corrupt the tree.
- [x] B6 (unverified) [fixed, desktop check pending]: BuildSetupBoneMatrices does not check the linear-bone table
  exists or matches numbones.

Wrong behaviour users will see:
- [x] B7 (confirmed) [fixed, desktop check pending]: any VMT containing the substring `$color` is treated as
  tintable (`materialloader.cpp` around 274). On opaque DXT1 textures, the
  tint multiplies alpha=255 and can turn the whole texture white.
- [x] B8 (confirmed) [fixed, desktop check pending]: changing the Skin family only calls `update()`
  (`mainwindow.cpp:1088-1094`). The texture index is computed once in
  `RebuildMeshBuffers`, so the visible skin does not change. Should refresh the
  viewport model.
- [x] B9 (confirmed) [fixed, desktop check pending]: `$basetexture` detection uses `contains`, so
  `$basetexturetransform` or `$basetexture2` lines can be taken as the path.
  Should compare the key exactly.
- [x] B10 (confirmed) [fixed, desktop check pending]: opening a model never calls `SetPlaybackSpeed`, so the
  speed slider and the real playback rate disagree after a model change.
- [x] B11 (confirmed) [fixed, desktop check pending]: during playback the Frame slider does not move; only the
  label updates (`OnAnimationTick`).
- [x] B12 (confirmed) [fixed, desktop check pending]: FPS and frame labels are not refreshed when a new model is
  opened (the population guard blocks the sequence-selected handler).
- [x] B13 (confirmed) [fixed, desktop check pending]: bodygroup labels are parented to the page widget, so the
  previous model's labels remain visible when a new bodygroup model loads.
- [x] B14 (confirmed) [fixed, desktop check pending]: Ctrl-W toggles wireframe, but the Render Mode combo keeps
  showing Textured; the sync is one-way.
- [x] B15 (confirmed) [fixed, desktop check pending]: language menu shows the new entry as checked before
  `LoadLanguage` succeeds; if the catalog fails to load, the menu lies and no
  message is shown.
- [x] B16 (confirmed) [fixed, desktop check pending]: mesh count shown in the status bar uses the bodygroup's
  visible count, while triangle count covers all variants.
- [x] B17 (unverified) [fixed, desktop check pending]: Load Weapon is allowed with no main model loaded, and the
  status shows "0/N bones matched" with no explanation.
- [x] B18 (unverified) [fixed, desktop check pending]: the Control Panel dock has an empty title bar widget, so
  a floating panel has no title bar to drag or re-dock from.
- [x] B19 [fixed, no animated VTF sample tested]: multi-frame VTF offsets
  did not account for frame count and forced the last frame. Now follows the
  VTF layout (mips smallest-first, then frame, face).

Minor and fragile:
- [x] B20 (confirmed) [fixed, desktop check pending]: formats 17, 18, 19, 21, 22 are decoded as 2 bytes per
  pixel, and the default branch emits white but reports "resolved".
- [x] B21 (unverified) [fixed, desktop check pending]: default colour parsing accepts only space-separated
  numbers, not `[r g b]`.
- [x] B22 (unverified) [fixed, desktop check pending]: `AttachHandsModel` deletes the old hands model before
  clearing the viewport pointer. Currently safe, but fragile.
- [x] B23 (unverified) [fixed, desktop check pending]: recent-file menu action calls OpenModel, which clears
  that same menu while the action is running.
- [x] B24 [fixed, desktop check pending]: Destructor deletes the model before the viewport, which still holds
  a raw pointer. Not dereferenced today.

## Process
- B1-B4 hardened and covered by selftest malformed-input cases (6 corrupted .mdl
  headers, truncated files, corrupted .vvd/.vtx). Deeper engine-side structures
  (anim data, ikrules, seqdesc sub-tables read through CStudioHdr/IBoneSetup)
  are not validated yet.
- Verify each "unverified" item before fixing or closing it.
- Each parity item is closed only with a check, a selftest, or a direct workflow
  comparison against the original viewer.
