# Hammer Qt Roadmap

Goal: migrate the original `../hammer` workflows into the Qt editor in small,
verified steps. Checked items are implemented and covered by an automated test
or a direct workflow check; partial support stays explicitly unchecked.

## Approximate Original-Hammer Coverage
Static coverage is 10 of 20 equally weighted stock workflows (50%).
A usable basic workflow earns one point, an implementation with material
restrictions earns half a point, and a missing workflow earns zero:

- One point each: box-brush drawing, selection and undo, point-entity placement,
  basic map checks and leak-path inspection.
- Half a point each: VMF compatibility, view and camera navigation, brush
  transforms, clipping and hollowing, vertex editing, brush entities, entity
  properties and I/O (including Entity Report), materials, groups and visgroups,
  compilation, game configuration, displacement editing.
- Zero each: decals and overlays, prefabs, cordon
  editing, model and sound browsers.

The 3D geometry brush and sewing extend the displacement workflow. It
continues to earn half a point while major parts of original Hammer's
displacement workflow remain missing. Reaching 60% under this rubric
requires two additional points and direct checks of the workflows being credited.
Prioritize original Hammer parity and fixes to its behavior before adding
features outside the original editor.

## Working Workflows
- [ ] Qt UI uses a shared dark theme and a 2x2 viewport layout modeled on the
  original editor. View maximize/restore and offscreen rendering are covered by
  automated checks; hands-on desktop evaluation remains pending.
- [ ] Hammer++ onboarding and discoverability: file/edit toolbars show text
  labels and Undo/Redo, working tool buttons expose their shortcuts, and the
  placement class is distinguished from the selected entity class. Empty entity
  properties explain what to select; material browsing explains scanning and
  active-material selection. Tie to Entity is also exposed in the Selection
  menu, and Shift+C activates the Camera tool.
  Tool-dependent 2D/3D hints prioritize the current mouse action in narrow panes
  and retain full instructions in tooltips. Compact-viewport rendering and
  interaction-hint regression checks cover this presentation. Integrated desktop
  acceptance at about 1178x754 with roughly 200-pixel viewport widths confirmed
  readable selection/block hints, a disabled "Select an entity" empty state,
  and no overlap between toolbars and the material empty state.
  A Hammer++-experienced author still needs to complete a fresh-profile workflow
  (brush, entity, material, save and compile) without verbal coaching before
  claiming that the interface is familiar or easy to use.
- [x] Load the configured FGD and place point entities with FGD defaults.
- [ ] Basic brush entities: tie selected world brushes to an FGD solid class,
  move them back to world, select and move as an entity, edit properties and
  outputs, copy/paste/delete and undo/redo. VMF load/save retains the owner,
  face data and missing explicit origin; unsupported nested content and
  ambiguous object IDs are rejected. Selected brush entities participate in
  grid snapping, bulk material application and selection-scoped material
  replacement; the face material/UV editor can choose one visible child brush
  by ID when the selected entity contains several. Axis-aligned box children can be scaled, rotated
  in 90-degree steps or mirrored together about their shared selection center,
  preserving ownership and undo; mixed point entities, non-box geometry and
  Hammer++ vertex caches are rejected. Axis-aligned clipping of convex child
  brushes and hollowing of box children preserve ownership and undo; operations
  validate the entire selection before changing it. Automated regression tests
  cover these paths; integrated desktop and separate VBSP checks remain pending.
  Full original brush-entity editing, arbitrary geometry operations, visgroups,
  complex maps and integrated desktop/in-game verification remain pending.
- [x] Draw axis-aligned world brushes in 2D, snap to grid, select, move, and delete.
- [x] Generate VBSP-compatible brush windings and reserve worldspawn ID 1.
- [x] Basic undo/redo for entity/brush creation, movement, deletion, property and
  material edits; group one mouse drag into a single undo step.
- [x] Multi-object entity/brush copy, cut and paste with new object/face IDs,
  shared offset, undo/redo and preserved entity names/I/O, as in original
  Hammer's ordinary paste. View-centered and special paste remain pending.
- [x] Select point entities by property key/value using case-insensitive exact or
  substring matching and an optional any-key search; selftest passed, desktop
  verification pending. Full Entity Report remains pending.
- [x] Multi-selection model for entities and world brushes; Shift-click toggles,
  Ctrl+A selects all, Ctrl+I inverts, and selected objects move/delete/material-
  apply together with undo restoring the selection. Same-class entity selections
  can edit shared FGD fields or keys present on every entity, in one undo step;
  unset, explicit empty and different values are distinguished. Mixed entity
  classes and mixed brush/entity property editing remain unavailable.
- [x] Finish 2D move transactions on release, lost focus/capture, hide and mouse
  mode changes; all selected objects are highlighted in both 2D and 3D views.
- [x] F9 compiler pipeline runs VBSP, VVIS and VRAD, detects VBSP leaks, and copies
  the BSP to the selected game's maps folder. A simple sealed test map compiled
  for GMod and the user confirmed it runs in game. Full Run Map dialog workflow
  and automatic game launch remain unverified.
- [x] Basic texture-name selection/application to all faces of one selected brush;
  selected material is used for new brushes.
- [x] VMF entity I/O connections preserve ordering and repeated output keys.
  The save guard refuses in-memory values with characters that Source's
  TokenReader cannot safely represent, rather than writing a corrupt VMF.
- [x] 2D middle-button pan, cursor-anchored wheel zoom, and Shift+Z maximize/restore
  for the last active 2D/3D viewport; navigation has regression coverage, while
  integrated desktop interaction remains pending.
- [x] Failed VMF opens preserve the active document/history; unsupported nested
  content is rejected instead of silently discarded; saves use atomic replacement.
- [x] Before writing a VMF, reject non-finite geometry/UV values and text with
  double quotes, backslashes, line breaks or NUL. The error identifies the
  affected entity/brush or metadata category, and preserves the existing file.
  Original TokenReader tests cover quotes and backslash behavior. Existing maps
  with such values may open, but those values must be corrected before saving;
  per-game VBSP parser differences have not been exhaustively verified.
- [x] External language catalogs in `hammerlang/hammer_<locale>.qm` beside the
  executable; built-in English and Simplified Chinese, saved language preference,
  and language selection with restart. Catalog loading is covered by selftest.
- [x] Map Properties: staged worldspawn key/value editor populated from the FGD,
  with custom keys and batch undo/redo. Choices and flags use value-preserving
  typed controls; original property pane integration remains pending.
- [x] Entity properties show all FGD fields, including unset keys, friendly
  names and existing custom keys. FGD choices and flags use value-preserving
  controls; other typed controls and desktop verification remain pending. Flags
  toggle one entry per opening and preserve unknown bits.
- [x] Map Properties supports filtering by FGD label or internal key and switching
  between labels and raw keys; worldspawn choices and flags preserve unknown
  values. Desktop verification remains pending.
- [x] 3D flat and wireframe modes reconstruct convex brush face polygons for
  preview and picking, based on the plane-winding approach in original
  `hammer/brushops.cpp` and `hammer/mapsolid.cpp`. Degenerate or unsupported
  brushes fall back to bounds/wireframe behavior. QPainter depth sorting is
  approximate; real textures and accurate occlusion remain pending. This is
  not the complete original geometry model; offscreen rendering selftest passes.
- [ ] Experimental 3D Textured preview uses the configured game's loose/VPK
  base textures and VMF face UV axes. The 3D viewport's right-click menu offers
  Textured, Flat and Wireframe; an available game configuration selects Textured
  initially, and unavailable OpenGL falls back to software Flat. This is not
  the Source material system. Simple Patch VMT includes and scalar insert/replace
  are supported with cycle/depth limits. A top-level Proxies block is ignored
  for static base-texture preview; dynamic proxies are not executed. Conditional
  directives, other nested sections, unsupported shaders and malformed VMTs
  fall back to flat color. Lightmaps,
  transparency and model rendering remain limited or unsupported. Displacement
  surfaces render with base texture coordinates; blend alpha is not previewed. Validate
  real maps, per-game search-path behavior and original-Hammer parity.
- [x] 3D camera navigation supports D/C and arrow keys, with Shift+arrows for
  camera-relative movement. Z toggles mouse-look with a center crosshair and
  WASD movement; Escape, focus loss, window deactivation or hiding the view
  exits it. Movement uses fixed steps and system key repeat rather than original
  frame-based acceleration; mouse sensitivity and reverse-Y options are not
  exposed. Mouse-look recenters the pointer without OS cursor confinement, and
  viewport clicks are suppressed while it is active. The original editor's full
  navigation options and interactive desktop verification remain pending.
- [x] The 3D viewport context menu can show or hide the ground grid and axes in
  Flat and Wireframe modes; this Qt enhancement saves the preference between
  sessions. Textured mode does not draw the grid because its software overlay
  cannot depth-test it against brushes. Rendering and persistence have selftests.
- [x] In the 3D viewport, drag a selected object with the selection tool to move
  it in the camera plane, with grid snapping and a single undo; Escape or loss
  of focus cancels. With the material tool, right-click applies the current
  material name to the nearest supported face; Shift+right-click applies the
  name to all faces on the hit brush as one undo step. Original Hammer's apply
  settings (including UV/lightmap and Alt edge alignment) remain pending. In
  the material tool, left click selects a face, Ctrl-click toggles additional
  faces, Enter applies the active material in one undo step, and Esc clears
  the face selection. Flat and textured views highlight selected faces; the
  textured highlight uses the same depth-tested geometry. Offscreen selftests
  cover selection, occlusion, hidden faces and undo; integrated desktop and real
  map testing, full original face-selection/UV editing and 3D handles remain pending.
- [x] Optional 2D arrow-key selection nudging preserves undo and grid behavior;
  opt-in state, view axes, fine movement and undo are covered. Shift-drag cloning
  and desktop verification remain pending.
- [x] Users can select multiple FGD files and restore their choice; selection,
  ordered multi-FGD loading and saved paths are covered by selftests. Full game
  configuration, per-game profiles and asset archive selection remain pending.

- [x] Entity Report has a modeless point/brush-entity list, class and key/value
  filters, multi-selection, delete with undo and view centering. Class completion
  includes FGD and map classes; columns can be sorted and results are counted.
  An optional selected-entities-only filter is a Qt-editor enhancement; empty
  value searches match existing keys, or empty values with exact matching, and
  target names display regardless of key capitalization.
  An entity-type filter narrows results to all, point or brush entities using
  actual brush ownership, including classes missing from the FGD. Select, Go
  to and Properties act on the current rows while the report stays open. Go to
  centers on selection bounds, including brush entities. Changing maps clears
  the report's row selection. Hidden-entity filters and desktop verification
  remain pending.
- [x] Grid visibility and power-of-two size commands (1–512) are implemented;
  selftest covers state and rendering; desktop shortcuts and menu behavior still
  need direct verification.
- [x] Grid Settings dialog edits the existing power-of-two grid size, grid
  visibility and snapping together. Unsupported sizes are rejected, and the
  existing menu/document state remains synchronized; custom sizes are pending.
- [ ] Basic View Filter toggles world brushes, point entities and entity names,
  and filters entity class names by substring. Rendering and picking honor the
  filter in supported 2D/3D workflows. This is transient view state, not original
  Hammer visgroups or complete selection filtering. Quick Hide can temporarily
  hide selected or unselected world brushes and point entities, and Quick Unhide
  clears those hidden IDs. This view state is not written to the VMF and is not
  a visgroup; other filters can still hide objects after Quick Unhide.
- [ ] VMF visgroup definitions (including nesting), membership keys, visibility
  flags and `hidden` wrappers are retained on load/save and influence visibility
  and picking. A session-only override can show objects hidden by saved flags;
  it does not edit VMF data. A basic Visgroups dialog can create root/child
  definitions, rename them, and add/remove selected visible objects, with undo;
  it preserves unrelated memberships and reserves unknown referenced IDs.
  Removing a color-visgroup assignment and editing hidden objects are refused.
  Show/Hide Group temporarily affects members of the selected group and nested
  groups; for overlapping membership the most recent action determines the
  object's session visibility. Brush-entity visibility propagates to children,
  while Quick Hide still takes precedence. This is view-only: saved editor flags
  and hidden wrappers are not changed, and reopen resets overrides. A leaf group
  can be deleted with one undo while removing its membership references; groups
  with children, ambiguous IDs, and unsafe hidden/color membership are rejected.
  The dialog can select the visible direct members of one group, mapping brush
  entity children to their owning entity. As in original Hammer's Mark command,
  child visgroups are not included; unlike original Hammer, no visible members
  or an ambiguous group ID preserves the previous selection. Automated checks
  cover selection and filtering; desktop interaction has not been verified.
  Offscreen tests cover selection safety and VMF preservation. Deleting parent
  groups, persistent visibility editing and the original visgroup UI remain pending.
  Tie/untie refuses objects with group/hidden metadata,
  and pasting such clipboard objects into a different document is refused to
  avoid changing group ownership or visibility. Plain cross-map paste still works.
  An independent GMod VBSP fixture confirmed saved `hidden` world and entity
  wrappers are not compiled. Desktop and in-game verification are pending.
- [x] Basic `.lin`/`.pts` XYZ pointfiles load transactionally, draw in 2D and 3D,
  and navigate cyclically with Home/End in a viewport. A fresh `.lin` is loaded
  after a detected VBSP leak; malformed or empty files preserve the existing
  path instead of replacing it with an empty path.
  Integrated desktop verification remains pending; richer point markers and
  file-format variants beyond one XYZ triplet per line are not supported.
- [x] Escape cancels an active 2D move transaction or clears the selection.
  Automated coverage exists; direct interaction verification remains pending.
- [x] Center on Selection centers all viewports on the selected object bounds
  with Ctrl+E; automated coverage exists, desktop verification remains pending.
- [x] Each 2D view's context menu can switch between XY, XZ and YZ projections.
  Changing projection preserves the view's world center and zoom and safely
  ends an active drag; projection choices are not persisted. Integrated desktop
  verification remains pending.
- [x] View menu commands can center all 2D views on the selection or the 3D
  camera position without moving the camera. A separate persistent, default-off
  option keeps the three 2D views centered on changes to the camera eye; disabling
  it restores independent navigation. Point entity bounds use their origins.
- [x] Center 3D View on Selection (Ctrl+Shift+E) positions the 3D camera at
  the original Hammer offset of 100 units along negative Y and points at the
  selection, leaving 2D views unchanged. Empty selections do nothing; point
  entity bounds are approximated by origins. Desktop verification remains pending.
- [x] Snap Selected to Grid supports whole-selection and individual movement,
  preserving brush dimensions and undo. Mixed selection currently approximates
  point-entity bounds by their origins; desktop verification remains pending.
- [x] Center Brush Entity Origins moves explicitly defined origins to the center
  of each selected entity's brush bounds without moving brushes, with one undo.
  Implicit origins and point entities are skipped; original helper semantics
  and desktop verification remain pending.
- [x] Paste Special supports a limited offset/copy array with a single undo;
  mixed entity/brush copies receive new IDs and preserve names and I/O. Get
  Offset fills each axis from the copied bounds, clicking again reverses it;
  point entities contribute origins rather than full helper bounds. Optional
  view-centered placement uses top-view X/Y and front-view Z, preserving grid
  alignment; it does not yet use the active view or cursor like original Hammer.
  Accepted copy count, offsets and view-centering preference persist between
  dialog openings; cancellation leaves the saved settings unchanged. Original
  rotation and naming controls remain pending.
- [x] Entity outputs have a basic staged ordered-list editor with undo; the
  selected connections can be copied and pasted across output dialogs and
  deleted in batches or moved up/down while retaining their raw values and
  relative order. Reordering is a Qt enhancement. The process-local connection buffer preserves raw values
  and is separate from the system and map-object clipboards; closing the dialog
  without accepting discards its edits. The original structured input/output
  browser, entity picker and complete validation remain pending.
- [ ] View menu supports centering on space-separated XYZ coordinates and
  selecting a world brush by VMF ID (Ctrl+Shift+G). Source setpos/setang
  console strings position and orient the 3D view; brush-entity lookup remains pending.
- [x] Replace texture across all world brushes or only selected brushes using
  an explicit scope option, with exact or partial name matching and substitution;
  texture coordinate rescaling and the full browser workflow remain pending.
- [ ] Basic map checks detect missing player starts, invalid face texture axes,
  duplicate face IDs, duplicate nonzero AI node IDs, repeated same-facing
  plane normals on a brush, and solids that the preview
  winding algorithm cannot reconstruct. The problem list can select and center
  visible affected objects without closing; hidden/filtered objects are not
  selected. An optional persisted "visible objects only" setting checks the
  current View Filter and refreshes results immediately; this is not visgroup
  visibility. Go To refuses fully hidden brush entities and checks that the
  problem's object was selected before centering; desktop verification remains
  pending. Output checks also warn when a five-field connection's target is
  absent, matching named targets and Hammer-style prefix wildcards. Runtime `!`
  targets and ambiguous connection formats are skipped; dynamically created
  ordinary names and runtime classname targets can still be reported and require
  manual review. Original Hammer's output validation also checks target names,
  so it can similarly flag runtime classname targets. Duplicate plane detection
  follows the original helper's exact normal comparison; original Hammer leaves
  that helper's map-check pass disabled. This check is advisory, does not fix the
  brush and does not identify nearly parallel normals. Geometry
  warnings require manual review: the algorithm and its
  tolerances are not VBSP validation. Full geometry and leak checks remain pending.
- [x] Make Hollow supports inward/outward shells for axis-aligned world boxes and brush-entity box children,
  preserving face materials and undo; unsupported brush shapes and Hammer++
  vertex caches are rejected. General convex hollowing remains pending.
- [ ] Material list scans loose VMTs and VPK indexes in a selected game directory
  and ordinary `gameinfo.txt` GAME search paths, including wildcard entries.
  The selected game directory still has a compatibility scan of its VPKs, so
  listed materials are not guaranteed to be mounted. Selecting a scanned entry
  asynchronously decodes its base texture into a single CPU preview; double-click
  applies it to selected brushes. Unavailable or unsupported images show an
  explicit fallback. Full thumbnails, `#base`/`#include`, game-specific dynamic
  mounts and complete browser behavior remain pending. Offscreen fixture coverage
  exists; desktop and real-game GPU preview validation remain pending.
- [x] Material preview reloads a cached VPK directory index on subsequent asset
  lookups when the index file size or modification time changes; numbered chunk
  payloads are reopened on each lookup. Explicit GAME paths to standalone VPKs
  are read when no corresponding `_dir.vpk` exists, while split archive names
  still prefer that directory index. Already uploaded GPU textures do not
  refresh automatically, and a replacement preserving both size and modification
  time is not detected. Regression fixtures cover standalone archives and index
  and chunk replacement; this does not implement all game-specific mounts.
- [x] Selecting the same game directory again in the compile workflow reloads
  its material search paths and schedules uploaded textures for replacement.
  This covers changed gameinfo and supported mount configuration; there is no
  dedicated refresh command or automatic file watcher, and same-size,
  same-timestamp VPK index replacements remain undetected.
- [x] Entity property reset applies an FGD default with undo, including inherited,
  explicit empty and zero defaults; properties without an FGD default remain
  unchanged and custom keys are preserved.
- [x] Scale selected bounded convex world and brush-entity brushes around their
  shared center with positive per-axis factors and undo. Mixed point entities,
  invalid geometry and Hammer++ vertex caches are rejected. Scale texture
  locking remains pending.
- [x] Rotate selected bounded convex world and brush-entity brushes by 90 degrees in either direction
  around their shared center on the X, Y or Z axis, retaining face IDs and materials and
  locking texture coordinates by rotating UV axes and compensating offsets.
  This is one undo step. Mixed point entities, invalid geometry, Hammer++
  vertex caches, invalid coordinates and zero texture scales are rejected.
  General arbitrary-angle rotation remains pending; angle conventions have not
  yet been fully compared with original Hammer's Transform dialog.
- [x] Flip selected bounded convex world and brush-entity brushes around their shared center on X,
  Y or Z, preserving texture alignment with one undo step. Point-entity mixtures,
  invalid geometry and Hammer++ vertex caches are rejected. Scale, rotate and
  mirror share a sloped-brush regression covering VMF round trip and undo.
  A separate VBSP compile and desktop test are pending.
- [ ] The 2D vertex tool (Shift+V) displays handles for one selected visible
  convex world brush or one visible brush entity containing exactly one brush.
  Dragging moves all vertices sharing the handle's 2D
  projection in that view plane; release commits one undo, while Escape, focus
  loss and selection/tool changes cancel the preview. A candidate is rejected
  if face planarity, convex reconstruction, topology, finite bounds, texture
  axis area or scales are invalid, or it has a Hammer++ vertex cache. Face IDs,
  materials and UV values are preserved without texture locking. Offscreen
  regression checks cover a planar move, rejection, owner preservation and undo.
  A GMod fixture with a deformed single-brush `func_detail` compiled through
  VBSP/VVIS/VRAD without a reported leak; the existing `sky_day01_01` VTF
  flags/dimensions warning remained. In-game behavior was not tested.
  This is not the original full vertex/edge tool: multiple-brush entities,
  topology changes, arbitrary individual depth vertices, 3D handles and desktop
  interaction remain pending.
- [x] Clip selected bounded convex world and brush-entity brushes by an X/Y/Z coordinate, keeping
  one or both sides with undo. Mixed selections, cuts missing a selected brush,
  invalid geometry and Hammer++ vertex caches are rejected.
- [ ] A three-world-point dialog clips bounded convex world brushes and brush
  entities against an oblique plane, keeps either or both sides, and groups the
  operation into one undo. Invalid/degenerate input, unsupported vertex caches,
  point entities, and planes that do not cross every selected brush are rejected
  without partial edits. Automated geometry, VMF and undo checks cover the
  supported path; an independent GMod fixture compiled with VBSP/VVIS/VRAD.
  A basic 2D clip tool draws a line across selected brushes, lets either endpoint
  be dragged with grid snapping, cycles the kept side or both with Shift+X,
  applies with Enter, and cancels with Esc; undo, endpoint interactions and 2D
  projection paths have offscreen selftests. It is not the complete original
  clipping tool, and integrated desktop and in-game checks remain pending.
- [x] Edit individual face material and UV fields on a selected world brush or
  one visible brush chosen by ID from a selected brush entity,
  with undo. Selected dialog rows can align texture axes to world or face using
  the original orientation conventions while retaining scale, offset and stored
  rotation. As a Qt-editor enhancement, the current material can be applied to
  multiple selected rows in this dialog as one staged edit. Select all, invert
  and clear buttons adjust the selected face rows for material and alignment
  operations without changing the document until confirmation. The dialog can
  include faces from multiple selected visible brushes and brush entities, with
  one atomic undo; hidden children are not silently edited and mixed unsupported
  selections are refused. Offscreen tests cover the operation. The separate 3D
  face selection workflow applies materials to selected faces, but does not yet
  open this full UV editor; previews and desktop verification remain pending.
- [x] Entity outputs suggest FGD output/input names and existing map targets while
  preserving custom strings. Prefix-wildcard targets also suggest the union of
  matching named entities' inputs; this is advisory and does not implement the
  complete original I/O browser or validation. Desktop verification is pending.
- [x] VMF versioninfo key/value metadata is preserved during round trips; desktop
  verification remains pending.
- [x] VMF cordon key/value data is preserved on load/save; active cordons block
  compilation because clipping and the cordon editing UI are not yet supported.
- [x] VMF viewsettings grid spacing, snapping and visibility load into the UI and
  persist on save; unsupported values and other settings are retained. Supported
  flat Hammer++ metadata and vertices_plus face caches are preserved; unsupported
  nested metadata is rejected. Other viewsettings behaviors remain pending.
- [x] VMF solid and point-entity editor colors and logical positions are preserved;
  supported visgroup and hidden data is retained as described above, and other
  unsupported data is rejected.

## Remaining Feature Work
- [ ] Selection filters and full original-Hammer clipboard
  behavior including view-centered placement, cross-process and special paste.
- [x] 2D region selection uses original `BoundBox` intersection or containment
  predicates on projected brush and entity bounds, with Shift selecting only
  fully enclosed objects. Group and visgroup selection modes remain pending.
- [x] VMF camera blocks preserve camera positions, look targets and active index
  on load/save. View > Saved Cameras lists cameras, saves the current 3D view,
  navigates to a valid entry, and deletes entries with undo/redo. Navigating to
  a different entry also updates the active camera in the document. In the
  Camera tool (Shift+C), Shift-drag in a 2D view adds a camera, and dragging
  either handle edits its eye or look target. Clicking an existing handle
  activates that camera in 3D. Escape cancels a pending drag. Saved position
  and look direction do not preserve camera roll; desktop verification remains
  pending.
- [ ] Full VMF brush-entity semantics, displacements, groups, visgroups, cordon editing, editor
  metadata, and unknown-block preservation. Current loader rejects unsupported
  constructs to protect the source file.
- [ ] Full texture browser: scan mounted game/VPK materials, thumbnails/previews,
  per-face selection, texture alignment, scale/rotation, and replacement. Current
  UI accepts a material path and lists a few HL2 examples without previews.
- [ ] Real 3D brush/entity rendering and materials, 3D picking/editing, camera
  navigation, models/helpers, lighting preview, and faithful 2D rendering.
- [ ] Brush transforms, resize handles, vertex editing, clipping, primitives,
  hollow/carve, and geometry validation. Current brush creation is axis-aligned.
- [ ] Full FGD-driven entity property controls, flags, entity I/O editor, brush
  entities, entity report, model/sound browsers, and target navigation.
- [ ] Displacement create/edit/paint/sew/subdivide; overlays, decals, paths,
  prefab library, groups, visgroups, and map-check/pointfile workflows.
- [ ] Basic displacement workflow supports validated Source `dispinfo`
  load/save for powers 2–4, retaining the stored rows for unedited fields.
  The 3D view renders the same grid for face picking and textured preview;
  orthographic views show the terrain outline and selected grid. Select faces
  with the Material tool and use Tools > Displacements to create on eligible
  quadrilateral world faces, remove terrain, or edit and smooth the per-vertex
  distance grid. Paint Geometry opens a modeless panel for spatial 3D
  Raise/Lower, Raise To and Smooth strokes on selected displacements, with
  radius and brush falloff controls. Sew joins compatible selected seams,
  including reversed edges, mixed powers and midpoint T-junctions.
  Map edits support undo. Additional original brush and axis options,
  alpha painting, subdivision, power changes and full material blending
  remain pending.
- [ ] Grid settings, view filters, auto-combine, and remaining
  menu commands that are still placeholders.
- [ ] Full game configuration UI with multiple games, FGDs, search paths and
  compiler profiles; expert compile options and game launch integration.
- [ ] Replace remaining lightweight document/model paths with original Hammer
  geometry/document infrastructure where it improves format compatibility.

## Original Hammer Backend Migration

The Qt application currently uses `app/mapmodel.*` and `app/vmfio.*` for its
document. It links the partial MFC-free `hammer_core` port and uses its
`BoundBox` for view-centering when the calculation preserves document precision,
falling back to the existing double-precision calculation otherwise. Production
document objects are not yet backed by that library. `core/wscript` builds the partial port
of original map-class, group, visgroup and geometry support, with an
`IDocNotify` bridge. Building that static library alone does not establish
that its symbols can be linked or that the editor uses its model. Original
`CMapDoc` inherits MFC `CDocument`, while the original solid, face, entity and
world implementations also reference document, view, rendering and texture
systems; replacing the window layer alone therefore does not decouple them.

- [ ] Extend the linked `BoundBox` smoke test to exercise original map-object
  creation, ownership and destruction, and resolve their dependencies before
  claiming the core object model is usable.
- [ ] Extend the initial MFC-free winding reconstruction, adapted from
  `hammer/brushops.cpp` and `CMapSolid` in `hammer/mapsolid.cpp`, beyond 3D
  preview and picking into a verified geometry model. Preserve original plane
  conventions and validate non-box convex brushes, degenerate input, bounds,
  edits and round trips against original Hammer and VBSP fixtures.
- [ ] Decouple `core/mapface.cpp` and `core/mapsolid.cpp` from MFC views,
  document globals and texture/render services using narrow host interfaces;
  validate IDs, material/UV data, geometry transforms and undo before wiring
  Qt editing to the original-backed geometry.
- [ ] Bring `core/mapentity.cpp` and `core/mapworld.cpp` into the linked model
  after isolating helpers, visgroups, overlays and document dependencies. Add
  brush entities and hierarchical ownership with load/edit/save/undo tests.
- [ ] Evaluate the original `public/chunkfile.*` VMF reader and writer against
  real and synthetic fixtures, including Unicode paths, repeated keys, unknown
  chunks and Hammer++ metadata. Keep atomic saves and reject unsupported data
  until the replacement can demonstrate no data loss; the low-level parser
  alone does not implement the original object model or full VMF semantics.
- [ ] Bridge the Qt document, selection, history and view notifications onto
  the verified core in small increments. Compare each operation to original
  Hammer and retain current regression coverage until parity is demonstrated.

## Verification Queue
- [x] User confirmed the compiled simple sealed GMod test map runs in game.
- [ ] Exercise maximize/restore and 2D pan in the integrated desktop UI; offscreen
  navigation and maximize/restore regression checks are covered.
- [ ] Compare complex original VMFs and round-trip every supported construct;
  add fixtures before extending support for each new block type.
- [ ] Launch an HL2 map and verify spawn, movement, textures and lighting in game.
- [ ] Compare migrated workflows against original Hammer, including error paths.
- [ ] Exercise language selection, restart, saved-map reopening and translated
  property editing in the desktop UI; add other languages as catalogs are ready.

## Hammer++ Reference and Candidate Enhancements

The following are research and planning items, **not implemented Hammer Qt
features**. Original Hammer parity and safe VMF round trips remain the baseline;
schedule enhancements only after their dependencies are in place. Hammer++
behavior can vary by Source branch, so verify the target game's behavior before
claiming compatibility. The editor-specific `editor_text` entity, for example,
is not an in-game entity.

References:
- User-supplied Valve Developer Community overview:
  https://developer.valvesoftware.com/wiki/Hammer%2B%2B
- Hammer++ official feature descriptions:
  https://ficool2.github.io/HammerPlusPlus-Website/features.html
- Hammer++ official downloads and branch-specific installation notes:
  https://ficool2.github.io/HammerPlusPlus-Website/download.html

### Editing and workflow candidates
- [x] Recover unsaved VMFs after a crash using a debounced atomic recovery copy
  and explicit restore/discard choice; restoring keeps the source path and dirty
  state, and never overwrites the original until the user explicitly saves.
- [x] Reset an entity key to its FGD default from the property editor; the
  action is undoable and preserves unknown/custom keys, including empty/zero
  defaults where declared by the FGD.
- [ ] Investigate precise brush vertex round trips and vertex-drift prevention;
  supported `vertices_plus` data is currently preserved and translated, not a
  complete implementation of Hammer++ brush accuracy or vertex editing.
- [ ] Add 3D transform gizmos and an adjustable pivot after robust 3D picking,
  transforms, geometry validation and undo are implemented.
- [ ] Investigate convex polygon drawing, three-point clipping and interactive
  vertex editing with invalid-face feedback after general brush support exists.
- [ ] Investigate convex brush merge, move-to-floor/ceiling with optional normal
  alignment, and fine or custom grid sizes with explicit precision safeguards.
- [ ] Explore face material UV-vector editing, quick nodraw and texture-shift
  controls after a full face-selection and material browser workflow exists.
- [ ] Explore 3D drag-selection, a configurable keybinding editor, and improved
  color picking with per-map palettes; retain safe keyboard and undo behavior.
- [ ] Support multiple editable cordons only with faithful compiler clipping and
  safe VMF semantics; preserved metadata alone is not a working cordon tool.
- [ ] Investigate creating instances from selections and viewing parent-map
  context while editing an instance after instance loading/editing is supported.

### Preview and browser candidates
- [ ] Evaluate interactive lighting and fog previews after real 3D rendering,
  game asset mounting and reliable per-branch lighting semantics are available.
- [ ] Investigate 2D/3D skybox, material proxy, render-effect, particle and rope
  previews with explicit per-feature toggles and graceful unsupported fallbacks.
- [ ] Add useful 3D grid and visibility toggles for tool textures, editor-only
  models and sprites after those objects are actually rendered.
- [ ] Explore model and particle browser improvements, richer filtering and
  attachment previews; add texture alpha-channel preview to the material browser.
- [ ] Investigate physics placement and pose preview only after model/entity
  rendering and a compatible physics simulation layer are available.

## Language Catalog Build
- Edit `hammer2/hammerlang/hammer_zh_CN.ts`; refresh sources with Qt `lupdate`.
- Build both targets with `python waf build --targets=hammer_qt,hammerlang_hammer_zh_CN`.
  The compiled catalog is `build/hammer2/app/hammerlang/hammer_zh_CN.qm`.
- Deploy the EXE to the chosen bin folder and the QM to its `hammerlang` child.

## Later Ideas
- [ ] MCP interface for document editing and map validation.
