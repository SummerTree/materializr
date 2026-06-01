# Changelog

All notable changes to Materializr are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow SemVer.

## [0.5.1] — 2026-05-31

### Added

- **v3 project file format — `.materializr` files are ~5× smaller.** Binary
  BREP via OCCT's `BinTools` (no display triangulation), gzip-wrapped at max
  compression, and a length-prefixed `PARAMS_LEN` block for op-parameter
  blobs so a single op can carry multiline / arbitrary content. The reader
  auto-detects v2 vs v3 by the gzip magic + header version; old files load
  unchanged, new saves write v3. A real-world 16 MB project saves at ~3 MB
  in v3.
- **Constraints panel on the live sketch.** Selecting a sketch (or any
  region of one) outside sketch-edit shows every constraint in the
  Properties panel:
  - Distance / Radius (shown as Ø diameter) / Angle constraints get inline
    text editors that commit on Enter or focus-out.
  - Non-dimensional constraints (Horizontal, Parallel, Coincident, etc.)
    appear as muted bullet rows so you can see what's applied.
  - Edits run the solver immediately and push a `SketchEditOp` covering
    before+after onto history (undoable).
  - Works across sessions — operates on the live sketch, not historical
    snapshots.
- **History → Properties editor survives reload.** `SketchEditOp` now
  serializes both before+after sketch snapshots into the project file's
  per-step params blob. On load, the rehydrator reconstructs a real
  `SketchEditOp` (binding to the live sketch by id) instead of a
  parameterless `ReplayOp`, so clicking a sketch-edit step in History on
  an old project still surfaces editable constraint values.

### Changed

- **`Sketch::setCircleRadius` / `setArcRadius`** mutators on Sketch — the
  constraint solver now actually writes the constraint value back into the
  circle/arc geometry when a Radius constraint applies, instead of treating
  Radius as "informational only" (the previous comment in the solver). So a
  Radius constraint set to 30 mm now produces a 30 mm circle, not a 30-mm-
  labelled 130-mm circle.
- **Taskbar icon resolves correctly under Dash-to-Panel / GNOME.** The GLFW
  window now sets `WM_CLASS = "Materializr"` (X11) and `app-id =
  "Materializr"` (Wayland) via `glfwWindowHintString`, and the bundled
  `.desktop` includes `StartupWMClass=Materializr` so the running window
  matches its launcher entry. Without these, GLFW defaulted to `glfw` and
  Dash-to-Panel showed a generic blank.

### Fixed

- **Document::findSketchId(Sketch*)** helper added so callers can do a
  reverse lookup from a held `shared_ptr<Sketch>` back to its document id —
  used by `SketchEditOp::serializeWithDocument` to stamp the live id into
  saved snapshots.
- **Parser refactor**: extracted `parseSketchBody(istream, Sketch&,
  endTok)` out of the existing `readSketch` so the same parser serves both
  top-level sketches and embedded SketchEditOp snapshots. No behavioural
  change for v2 files.

### Known limitations

- **Downstream ops don't auto-cascade yet.** Editing a sketch's constraint
  resizes the sketch correctly, but an `ExtrudeOp` / `PushPullOp` /
  `FilletOp` that consumed that sketch (or its resulting body) doesn't
  automatically re-execute. The body that came from the old sketch keeps
  its old shape until you redo the op manually. Tracked as task #98
  "replay-on-sketch-edit"; the toponaming work needed to safely re-run
  fillets etc. is being treated as its own future release.

## [0.5.0] — 2026-05-31

### Added

- **Loft plugin.** New solid op that morphs between two closed sketch
  profiles via `BRepOffsetAPI_ThruSections`. The plugin contributes a "Loft"
  button to the Sketch panel AND the Region panel — select two whole
  sketches or two regions on different sketches and click. With only one
  selected, a non-modal banner nudges you to Ctrl-click a second profile
  (the previous modal version blocked viewport picks). On click with two
  selected, a popup opens with **Solid / Shell**, **Smooth / Ruled**, and
  **Reverse profile B vertex order** toggles, each re-pushing a live preview
  `LoftOp` onto history; Apply commits, Cancel/Esc undoes the preview.
  Tooltip explains the "parallel-plane assumption" so users understand why
  orthogonal-plane lofts produce a pyramid-tent surface.
- **Sketch-as-construction-plane** workflow. A standalone sketch can now
  serve as a movable construction plane:
  - The viewport picker reaches sketches via either their closed-region
    interior (`SelectionType::SketchRegion`) or any edge (`SelectionType::
    Sketch`) — open profiles like an arc or a spline are now selectable too.
  - Box-select also captures sketches by projecting their points to
    screen-space.
  - When a sketch is selected outside ortho / sketch-edit, clicking **Move**
    or **Rotate** in the Tools panel arms a Move/Rotate gizmo centred on
    the sketch's bbox centroid. Dragging mutates the sketch's plane (and
    its geometry rides along) via a new `SketchTransformOp`, snapshotted
    for undo. A three-input X/Y/Z popup offers exact translations.
  - Selection-change disarms the gizmo automatically, so picking a sketch
    just surfaces the toolbar options first — no surprise gizmo on top.
- **Construction Plane popup.** Replaces the no-op New Construction Plane
  button: a live-previewed popup with **XY / XZ / YZ** radio buttons, an
  optional **Parallel to selected face** mode (auto-enabled when a planar
  face is in the selection), and an **Offset** slider that pushes the plane
  along its normal. Backed by `ConstructionPlaneOp` on history. (Plane
  rendering in the viewport + Items-panel listing remain a TODO — for now
  use the sketch-as-plane workflow above.)
- **Editable dimension constraints from the History panel.** Clicking a
  sketch-edit step in History now shows every Distance / Radius / Angle
  constraint with inline `InputDouble` fields (diameter for radius,
  matching the in-sketch popup). Typing a value re-solves the snapshot
  immediately; Apply Changes propagates it through `editStep`. Non-
  dimensional constraints (Horizontal, Parallel, …) appear as read-only
  rows.
- **Snap-grid corner widget** next to the ViewCube. Small square showing
  the current grid step (0.1 / 0.5 / 1 / 10 mm). Solid-blue border when
  snap is on, faint grey when off. Left-click opens a Snap & Grid popup
  (snap toggle + step buttons); right-click quick-toggles snap. Every
  change persists to `settings.cfg`, so the choice survives launches.
- **Persistent snap & grid settings** (`snapToGrid`, `sketchGridStep`)
  written to the same `~/.config/materializr/settings.cfg` as the other
  preferences, round-tripping through the JSON import/export path too.
- **App icon + banner.** Replaced the placeholder "C" SVG with the real
  M-cube `icon.png`, resized to 256×256 + 512×512 hicolor sizes by
  ImageMagick during the Docker build. `banner.png` lives at the repo
  root for the GitHub social-preview image.

### Changed

- **Sketch on XY / XZ / YZ now produce visibly distinct camera views.**
  `alignCameraToActiveSketch`'s continuity-projection used to snap the up
  vector to the same in-plane axis for both XY and XZ when starting from
  an empty viewport, making them indistinguishable. The three explicit
  toolbar entry points now prime the camera up with the canonical
  Top / Front / Right axis before entering, so the projection lands on
  the right `faceY` for each.
- **Gizmo readout shows the dragged axis** and matches the Z-up user
  convention (red = X, green = Z, blue = Y) instead of the world internals.
  Translate label leads with the axis letter and the coord tuple is
  re-ordered to `(user X, user Y, user Z)`. Rotate label does the same
  for its axis letter.
- **Gizmo translate snap is now absolute-position** (pivot lands on grid
  intersections) instead of delta-snap. Matches the rest of the app's
  grid-snap semantics; off-grid starting positions jump onto grid on the
  first qualifying drag. Rotate snap is hard 15° when snap-to-grid is on,
  free with a 7° soft-snap near 45° when off.
- **Gizmo dimension line draws from the world origin** to the current
  pivot, with the absolute coords in the label — easier to read "where
  is this now" than "how far did I drag this".
- **Gizmo hides during any interactive op** (Push/Pull, Loft, Construction
  Plane, Pattern, Shell, Resize, sketch-edit, …). Previously the rotate
  gizmo stuck around on top of those popups' previews.
- **Toolbar tooltips wrap across multiple lines** instead of running off
  one line. Long blurbs (Loft, Move/Rotate on sketches, the parallel-plane
  caveat) now read as paragraphs.
- **Sketch selection highlight covers open profiles.** `SketchRenderer::
  renderSketchHighlight` walks every primitive (lines / circles / arcs /
  splines / polygon edges) in solid yellow at 4 px when a whole sketch is
  selected, so an arch or open polyline lights up the same way a closed
  region does.

### Fixed

- **Vertical / open sketches were unpickable** in the viewport. Two causes:
  the picker only tested closed regions, and the body-vs-sketch occlusion
  rejected sketches lying on a body face because the mesh-triangle hit and
  the analytical plane hit can disagree by microns. Picker now falls back
  to an edge-distance test (returns the parent sketch as `SelectionType::
  Sketch`) and the occlusion threshold loosened to `max(0.5 mm, 0.5 %
  view-distance)` with an automatic source-face exemption.
- **Push/pull on a reloaded sketch around an existing hole** produced a
  solid bar instead of a tube. `Sketch::buildRegions` now grafts inner
  wires from the source face onto each sketch face whose outer wire fully
  contains them, fixing BOPAlgo not partitioning when edges don't intersect.

## [0.4.0] — 2026-05-30

### Added

- **SketchUp-style drawing-time inferences.** While you place sketch points,
  ghost guide lines show what the cursor is aligned to and the cursor snaps to
  that alignment — without leaving any persistent constraint behind. Seven
  inference kinds: endpoint (yellow square), midpoint (cyan triangle), on-line
  (lavender diamond), axis-from-point red horizontal and green vertical
  dashed guides, perpendicular-to-previous (orange dashed), and parallel-to
  -previous (magenta dashed). When two inferences fire at once (e.g. the line
  is perpendicular to the previous segment AND the cursor's X is aligned with
  an earlier point) the cursor snaps to their **intersection**, which makes
  drawing a square or any axis-aligned closed shape effectively one click per
  corner. Every guide is haloed in black so it stays visible against the
  light-blue sketch face / dark grid backdrop.
- **Auto-close on chain-start.** Clicking back onto the starting point of a
  line chain commits the closing segment and ends placement. Combines with
  the endpoint inference so you don't have to aim precisely — get within snap
  range, click, done.
- **Opt-in formal sketch constraints.** Right-click any selected sketch
  element(s) and a context menu offers "Add Constraint ▸" with seven types:
  Horizontal, Vertical, Coincident, Parallel, Perpendicular, Equal length,
  Fix Position. Items are filtered by selection arity so you only ever see
  options that can actually apply. Once added, a constraint enforces itself
  during subsequent drags (e.g. dragging a Horizontal line can shorten or
  reposition it but never tilt it). Constraints round-trip through the
  project file so they survive save/reload. Geometry you never click a
  constraint on stays totally free — constraints are 100% opt-in.

### Changed

- **No more silent autoConstrain.** Newly drawn lines used to pick up
  invisible Horizontal / Vertical / Coincident constraints based on their
  drawn angle. They no longer do — the inference system above provides the
  alignment help drawing-time, and persistent constraints come only from
  explicit user clicks.
- **Sketch line palette.** Committed sketch lines (lines / circles / arcs) are
  now deep cobalt (was pastel blue, washed out against the sketch face tint).
  Preview lines are bright yellow (was white, indistinguishable from the
  dimension overlay). Dimension overlay stays grey-white. Three colours, three
  clearly distinct meanings.

## [0.3.5] — 2026-05-30

### Added

- **Per-body STL export.** Right-click any body in the viewport or its row
  in the Items panel and pick **Export STL…** to dump just that body to
  a file. The save dialog opens with the body's current name (as shown
  in the Items panel) as the default filename — `<body name>.stl`, with
  filesystem-unfriendly characters (`/ \ : * ? " < > |`) sanitised to
  `_`. Separate from File → Export STL, which still writes every visible
  body to one file; this one is for pulling individual parts out of a
  multi-body project without juggling visibility.

### Internal

- New `Application::exportBodyAsStl(int bodyId)`, called from both
  context menus.
- `ItemsPanel` gained a `setExportStlCallback(std::function<void(int)>)`
  hook so it can route the menu click without depending on the STL I/O
  module directly.

## [0.3.4] — 2026-05-30

Tiny cleanup release — drops scaffolding left over from troubleshooting in
0.3.3 that didn't end up driving any behaviour. No user-facing changes.

### Internal

- Removed the `wasOrtho` flag in `Camera::orbitLevel` (and its `(void)`
  discard). It was captured for an experiment that snapped the orbit
  target to world origin on ortho-exit; the experiment was reverted
  earlier in the same session in favour of preserving the sketch's
  grid-aligned anchor, so the variable served no purpose.
- Removed the `SavedCamera` struct, `m_savedCameraForSketch` member, and
  the `saveCameraInto` helper. The original purpose — restoring the
  pre-sketch camera in `exitSketchMode` — was deleted in 0.3.3 in
  favour of leaving the camera in place. The snapshot was still being
  captured at every sketch entry but nothing read it.

## [0.3.3] — 2026-05-30

A long session of polish: ViewCube redesign + axis triad + roll buttons,
ortho-snap respecting turntable, picker depth occlusion fix, sketch-tool
highlight, polygon preview / rotation / corner-snap, push/pull direction
correction on STEP imports, two-step Escape + Exit-Sketch discard button,
and op-parameter persistence in the project file.

### Added

- **ViewCube overhaul** to match FreeCAD's layout: smaller cube body, four
  cardinal triangle-arrow orbit buttons, two large sweeping corner arrows
  for 90° camera roll around the view axis, a home / reset-view button
  with a house glyph, and a coloured XYZ axis triad that rotates with the
  camera. Triad colours follow the world grid convention (X red,
  Y green = world up, Z blue = world forward).
- **Side-face labels** shortened to `L / F / R / B` so the smaller cube
  reads cleanly; Top / Bottom keep full labels.
- **Roll buttons** (`RollLeft` / `RollRight` actions) rotate the camera's
  up vector 90° around the view direction — a snapped ortho view stays
  snapped but spins in place.
- **Z-up turntable respect on Top / Bottom ortho snap.** The new ortho
  view's up vector is computed from the camera's horizontal forward
  direction so whatever was ahead of you before clicking the face ends
  up at the top of the screen. Front / Back / Left / Right snaps keep
  world +Y up (those are unambiguous).
- **Sketch-on-face turntable + axis snap.** When sketching on a horizontal
  face the projected camera up degenerates; we fall back to the camera's
  forward direction projected onto the plane, then snap to whichever of
  ±faceY / ±faceX is closest, so the sketch view always lands axis-aligned
  but in the quadrant the user was facing.
- **Sketch grid origin snapped to the nearest world-grid intersection**
  projected onto the sketch plane — grid lines coincide with whole-mm
  world positions on the face plane instead of being shifted by the
  face centre's fractional coords. Same snapped point becomes the camera
  target, so an orbit out of ortho pivots around the area you were
  sketching on.
- **Active sketch-tool highlight.** The current tool's button in the
  sketch toolbar gets a 2 px mid-grey (`#808080`) border so it's
  unambiguous at a glance which mode (Line / Circle / Rectangle / Arc /
  Spline / Polygon / Trim) is active.
- **Polygon tool overhaul.** Dimension popup label reads "Sides" (≥3) and
  typing a count + Enter updates the live preview without committing —
  the user can iterate on side count, rotation, and radius before clicking
  to commit. The cursor's direction from the centre sets the polygon's
  rotation so the first vertex lands exactly on the (snapped) cursor —
  same "corner snaps to grid" behaviour as the circle tool.
- **Two-step Escape in sketch mode.** First press cancels just the
  in-progress shape (line mid-stroke, polygon awaiting its second click,
  etc.); second press exits sketch mode entirely.
- **Exit Sketch (discard) button** under Finish Sketch. Rewinds history
  to before the sketch was entered, removes the sketch from the document
  if empty, and exits — leaves the body in its pre-sketch state. Useful
  for bailing on a half-built sketch.
- **Op parameter persistence in the project file.** Each saved history
  step gains an optional `PARAMS "blob"` line; FilletOp, ChamferOp,
  ResizeCylindricalOp, ShellOp, and PatternOp each implement
  `serializeParams` / `deserializeParams` to round-trip their scalar
  inputs (radius, distance, count, axis, origin, etc.). ReplayOp carries
  the blob across save / load so a future edit-after-reload pass has the
  data already on hand. Older project files without `PARAMS` lines load
  unchanged.

### Fixed

- **Picker depth occlusion: clicking edges through a face.** The previous
  view-direction depth tolerance scaled with view distance (4 % then
  0.5 %), which at typical CAD scales let edges of a 1 mm wall on the
  far side of the body remain "clickable through" the front face. Now
  the picker computes the face's surface normal at the hit point and
  rejects edge segments whose signed distance to the face's tangent
  plane is more than 0.3 mm behind the camera-side. The check is
  camera-angle-independent, so a 1 mm wall reads as 1 mm behind whether
  the view is shallow or steep.
- **Edges of small / zoomed-out faces stealing clicks** from the face
  centre. The 8 px edge-promotion threshold is now clamped to ¼ of the
  face's projected on-screen size, so a 20 px face has at most a 5 px
  edge zone (vs every interior pixel previously being within 8 px of
  some boundary edge).
- **Push / Pull direction wrong on STEP-imported horizontal faces.**
  `BRepGProp_Face::Normal` returns the geometric surface normal, which
  on imported geometry sometimes points INTO the body — so push went
  into the solid and pull cut into thin air. Push / Pull now probes the
  body's solid classifier on BOTH sides of the face at 1 mm; if forward
  is inside and back is outside it flips the normal. Same check fires
  for the live arrow display in `beginPushPull` so arrow and extrusion
  agree.
- **Sketch on STEP-imported face plane normal verified by the classifier.**
  Mirror of the push/pull fix at sketch-entry time so the sketch plane
  itself faces outward — fixes the case where push/pull on a sketch
  region had to fight an inverted underlying plane.
- **Orbit out of sketch ortho** resets the camera up vector to world +Y
  (was carrying over the sketch's chosen up, which caused the post-orbit
  view to look rotated / twisted) and preserves the snapped sketch
  anchor as the orbit target so the model stays framed.
- **Exit Sketch leaves the camera where it is.** Previously
  `exitSketchMode` force-restored the pre-sketch camera, yanking the user
  back to wherever they were before they clicked the face. Now exit
  feels like leaving ortho-snap: the area being looked at stays framed,
  only the sketch grid disappears.
- **Trackpad-mode gizmo drag suppressed camera drag.** When orbit and pan
  are both bound to the Left button (trackpad mode), the camera drag
  block fired in addition to gizmo drag — so trying to drag a gizmo
  handle also yawed the camera. The camera drag now checks
  `m_gizmoDragging` first and suppresses itself while the gizmo owns the
  interaction.
- **ViewCube click-through.** The sketch input block now also gates on
  `m_viewCube->wasHovered()`, mirroring the body picker. Clicking a roll
  arrow or face on the cube no longer leaks a line-draw to the sketch
  underneath.

### Changed

- **Side faces of the ViewCube** are labelled `L / F / R / B` (single
  letters) to fit the smaller cube comfortably.
- **`addPolygon`** gained a `rotationRad` parameter so the first vertex
  can be aligned with the cursor direction. Existing callers that don't
  pass it default to 0 rotation as before.

### Internal

- `Operation` gained `serializeParams()` / `deserializeParams()` virtuals
  (empty default). ReplayOp carries the blob across save / load via
  `setStoredParams` / `storedParams()`.
- `ProjectHistoryStep` gained an opaque `params` field. ProjectIO writes
  `PARAMS "blob"` per step when non-empty; load tolerates missing lines.

## [0.3.2] — 2026-05-30

Sketch pattern polish: circles + arcs now actually replicate, the radial
origin picker drops a yellow snap-to-grid dot inside the sketch, and the
whole popup previews live the way the body pattern popup does.

### Added

- **Live preview for sketch patterns.** Each parameter change (count,
  spacing, sweep angle, origin pick) immediately rebuilds the preview
  on the sketch. Apply commits the current state as a `SketchEditOp`;
  Cancel / Esc cleanly restores the pre-popup sketch.
- **In-sketch radial origin picker.** Replaces the coordinate text
  fields. Click *Pick origin in sketch* in the popup, then click in
  the viewport — a yellow dot with `(x, y)` follows the cursor on the
  sketch plane, snapped to the current grid step. Click commits the
  origin and re-previews; Esc bails out of pick mode.

### Fixed

- **Sketch patterns now replicate circles and arcs**, not just points
  and lines. A sketch made of (say) just circles previously patterned
  nothing visible because the apply loop only walked points / lines.
  The whole-sketch fallback (no element selection) now includes every
  primitive type.

### Internal

- Sketch pattern state grows a `before` snapshot + captured involved
  ids; preview frames restore the snapshot then re-apply the
  transform, so previews never accumulate. Commit diffs against the
  same snapshot to build the `SketchEditOp`.

## [0.3.1] — 2026-05-30

Toolbar polish + Linear / Radial Pattern popups + sketch-mode patterns +
Z-up axis labels.

### Added

- **Toolbar tooltips** on every button (built-in tools and plugin
  contributions). Hover for a one-line description. Toggle via
  Settings → General → Interface → Show toolbar tooltips.
- **Linear Pattern popup** (body): pick an axis (X / Y / Z), count, and
  spacing, with a slider that lives-previews each change.
- **Radial Pattern popup** (body): pick an axis, count, total angle, and
  the world-space origin of the rotation axis — interactively by clicking
  a *Pick axis origin in viewport* button which drops a yellow snap-to-
  grid dot on the picker plane (perpendicular to the chosen rotation
  axis). The picker plane is overlaid with a faint grid so the snap
  points are visible.
- **Sketch-mode Linear / Radial patterns.** Same popup style as body
  patterns, without the axis radio (the sketch is on a fixed plane).
  Linear copies along sketch +X by a user-set spacing; Radial rotates
  around a user-supplied sketch-coords (X, Y) origin. Undoable via the
  normal sketch-edit op.
- **Z-up axis convention** in user-facing axis radios (Pattern, Split):
  X = left / right, Y = forward / back, Z = up. The world stays Y-up
  internally; only labels swap.

### Fixed

- **Items-panel visibility, scroll jitter, popup focus** (already in
  0.3.0; called out again here since users reported related cases).
- **Split X / Y / Z axes** now match the Z-up label convention. *(Note:
  the 0.3.0 ad-hoc X↔Z swap is superseded by the proper user-axis →
  world-axis mapping in 0.3.1.)*
- **Plugin button visibility under face selection.** Split, Duplicate,
  Linear Pattern, Radial Pattern no longer appear when a face is
  selected — those are whole-body operations and would just confuse.
  Move / Rotate / Scale / Mirror still appear (they're useful with a
  face picked: they move the parent body).

### Changed

- **Plugin contribution struct** grows an optional `tooltip` string.
  Existing plugins compile unchanged; new plugin buttons opt in by
  passing a 7th brace-init field.
- **PluginContext** gains `requestInteractiveOp(name)` /
  `takeRequestedInteractiveOp()` — a small request channel a plugin
  can use to defer to Application's popup machinery. Currently used
  by PatternPlugin to hand off the Linear / Radial button to the
  interactive popup; future plugins that need viewport + UI plumbing
  can reuse the same hook.

### Internal

- New `Application::userAxisToWorldVec()` / `userAxisToWorldIdx()`
  helpers centralise the Z-up label → world-axis remap.
- `PatternOp::setRadialOrigin()` lets the rotation axis pass through
  an arbitrary world-space point instead of the implicit (0, 0, 0).

## [0.3.0] — 2026-05-29

Geometry-correctness pass on the cylindrical-resize op and the push/pull
direction, plus an Items-panel rework that adds folders and multi-select.
Merges friend's Settings tabs + JSON preferences + Command Palette removal.

### Added

- **Folders in the Items panel.** `+ Folder` button creates an empty folder;
  any body's right-click menu has a **Move to folder ▶** submenu listing
  existing folders, a `New folder…` option that prompts for a name and drops
  the body into the new folder, and (if the body is currently in one)
  a `(root — no folder)` option to lift it back out. Folders own a
  visibility checkbox and a colour swatch, both of which **cascade to every
  body inside**: hiding the folder hides all members; setting the folder
  colour overwrites every member's colour (re-customisable per body
  afterwards). Folder names are renamable via double-click or context menu.
  Project save / load round-trips folders; older project files load
  unchanged with all bodies at root.
- **Multi-select in the Items panel.** Plain click single-selects (and
  becomes the shift-click anchor); **Ctrl-click** toggles a body in/out of
  the selection; **Shift-click** range-selects from the anchor to the
  clicked body across folders + root in display order. Right-clicking a
  body that's part of a multi-selection makes the Move-to-folder submenu
  act on **every selected body at once** ( " — all selected" is shown on
  each menu entry to make this explicit).
- **`--verbose` / `-v` CLI flag** (with `--log <path>` to override the
  default `/tmp/materializr.log` destination). Flips a process-global
  `materializr::isVerbose()` and redirects stderr to the log file (line-
  buffered, so a crash mid-op still flushes recent traces). Useful when
  triaging modeling bugs — `[Resize]`, `[Push/Pull]`, etc. diagnostics
  land in a file that survives the session. Off by default; normal
  launches stay quiet.

### Fixed

- **Cylindrical resize on holes that pierce non-perpendicular caps.**
  Previously the fill ring used the cylindrical face's parametric V range
  as a flat-disc height, which is correct only when the cap is exactly
  perpendicular to the cylinder axis. For tilted-flat caps (common on
  STEP-imported parts) the ring stuck out past the cap on the high-V
  angles and fell short on the low-V angles, leaving a visible "washer"
  ring of old-radius material at the cap. For curved caps (NURBS / BSpline
  exits — e.g. a hole through a filleted block edge) the same thing
  happened, larger. New approach is topology-agnostic: locate the cap
  face adjacent to the cylinder's extremal-V wire edge, then clip an
  axially-padded ring with a half-space built from the cap. For planar
  caps the half-space is built from the cap's extracted `gp_Pln` (works
  for any orientation, including tilted); for non-planar caps we extrude
  an untrimmed face from the cap's underlying surface along the cylinder
  axis into a finite prism and clip with that. Either path validates the
  clipped volume strictly decreases and falls back to the legacy height-
  bounded ring if the clip degenerates. Handles plane / cone / sphere /
  torus / NURBS / BSpline caps uniformly without classifying the surface
  type.
- **Push/Pull direction on chamfer / fillet / cylinder side faces.** The
  arrow + extrusion direction used the UV-midpoint surface normal, which
  for a curved face is the surface tangent perpendicular at that one
  point — sloped for a cone, twisted for a torus. Looked like the
  direction "followed the polygon clicked" instead of a stable axis. Now
  detects `Geom_ConicalSurface` / `Geom_ToroidalSurface` /
  `Geom_CylindricalSurface` / `Geom_SurfaceOfRevolution` and uses the
  surface's natural rotation axis as the push/pull direction, sign-
  corrected so positive distance still pushes outward. Flat faces are
  unchanged. Mirrored in both the arrow display
  (`Application::beginPushPull`) and the executed extrusion
  (`PushPullOp`) so they agree.
- **Items-panel visibility checkbox didn't actually hide.** Toggling the
  per-body eye checkbox set the document flag but didn't mark meshes
  dirty, so the renderer kept drawing the now-hidden body. Same bug on
  the per-sketch checkbox. Both now mark dirty and rebuild on the next
  frame.
- **Items-panel scroll jitter under multi-select.** The "auto-scroll to
  newly-selected row" code re-issued `SetScrollHereY(0.5)` for every
  selected row whose id differed from the last frame's primary selection
  → all selected rows took turns scrolling themselves into view, twitching
  in the user's intended scroll direction. Auto-scroll now only fires
  when the selection contains exactly one body (which is the case where
  it actually matters — viewport-driven picks bringing a row into view).
- **Edit Diameter popup wasn't draggable.** The popup re-anchored its
  position every frame and carried `ImGuiWindowFlags_NoMove`, so any drag
  attempt was undone immediately. Now anchors only on first appearance
  (`ImGuiCond_Appearing`), drops `NoMove`, and gets a title bar so the
  drag affordance is obvious. Confirm / Cancel buttons unchanged.
- **New-folder popup got stuck on screen.**
  `ImGui::SetKeyboardFocusHere()` was called every frame, hijacking
  focus back to the input field continuously and starving the
  Create / Cancel buttons. Now fires only on the first frame the popup
  opens.

### Internal / refactor

- Deleted ~240 lines of dead code in `ResizeCylindricalOp.cpp` left over
  from an abandoned chamfer-detection approach (`detectCap`, `CapInfo`,
  `makeRevolvedFill`). The current cap-following implementation
  supersedes it. Cleaned up ~14 OpenCASCADE includes that were only
  needed by the removed code.
- New `core/Verbose.{h,cpp}` exposes a process-global verbose flag set
  by `main` from the CLI. `ResizeCylindricalOp` and friends now use a
  `MZLOG(...)` macro that no-ops unless the flag is set, so normal
  launches don't spam stderr.

## [0.2.3] — 2026-05-29

A polish release: clearer tool semantics, a proper close-project flow, an
auto-update check, a `--safe-mode` recovery flag, and a fix to the Windows
CI that was rebuilding OpenCASCADE from scratch on every push (~1.6 h per
build → minutes).

### Added

- **`--safe-mode` CLI flag** (aliases: `--safe-graphics`, `--low-graphics`).
  Brings the app up with rendering forced to known-safe defaults (MSAA off,
  mesh quality Low, default lights), autosave off, auto-open-last-project
  off, and update-check off. The safe values are written to the settings
  file, so subsequent normal launches stay recovered. Use it if a saved
  high-quality setting crashes the app at startup or a complex
  auto-opened project hangs a lower-core machine. `--help` / `-h` prints
  the available options. Works identically on Linux AppImage and the
  installed Windows build.
- **File → Close Project.** Prompts to save if there are unsaved changes
  (unless autosave is on, in which case it saves quietly first), then
  clears the document, history, selection, and project path to leave you
  at an empty viewport.
- **Settings → Open last project on launch.** When on, Materializr
  reopens whichever project you had open the last time you quit. Using
  File → Close Project before quitting clears the stored path, so the
  next launch starts empty.
- **Settings → Check for updates on launch.** Default on. Hits the GitHub
  releases API at startup and pops a small "newer release available"
  dialog with a one-click link to the release page when applicable.
  Off in `--safe-mode`; can be turned off in Settings for offline /
  portable use (the Help → Check for Updates manual path still works).
- **"Extrude From"** appears in **Face Operations** as well as the
  sketch-region tools. On a sketch it extrudes the region into a new body
  (unchanged); on a body face it extrudes that face's silhouette along
  its normal into a new body. Push/Pull remains the in-place
  modify-the-body tool; Extrude always creates a separate body.
- **Troubleshooting docs** in `docs/usage.md` covering rendering-induced
  crashes, with `--safe-mode` as the easy recovery path and the
  hand-edit / file-delete fallback for everything else.

### Changed

- **Boolean Ops (Union / Subtract / Intersect)** now only appear in the
  toolbar when at least two bodies are selected. With a single body
  picked they can't do anything, so showing them was just noise.
- **Push / Pull vs. Extrude semantics tightened.** Push/Pull modifies the
  body that owns the picked face / region. Extrude always creates a
  separate body. The Extrude plugin's redundant face-extrude button +
  "Extrude Face" palette command are gone — Push/Pull is the polished
  in-place tool, and "Extrude From" handles the new-body case.
- **"Extrude Sketch" button label → "Extrude From"** since the same
  button now also extrudes from a face.
- **Settings → Apply also closes the dialog** (previously it just
  persisted and kept the dialog open).

### Fixed

- **Windows CI was rebuilding OpenCASCADE from scratch on every push**
  (~1.6 h per build) because the `x-gha` vcpkg binary-cache backend was
  silently removed upstream and the workflow's `clear;x-gha,readwrite`
  was a no-op. Switched to a NuGet-on-GitHub-Packages binary cache. The
  first build after this fix still pays the cold OCCT compile but writes
  the binaries to the repo's NuGet feed; subsequent builds restore them
  in a few minutes. Permissions for the workflow now include
  `packages: write` so the cache writes are allowed.

### Removed

- **Offset Face** is gone. Its implementation was the same OCCT call
  (`BRepOffsetAPI_MakeThickSolid::MakeThickSolidByJoin`) as Shell, with
  the picked face in the "face to remove" list — meaning it hollowed the
  body rather than offsetting a single face. Even if it were
  reimplemented to do what its name implied (move one face along its
  normal), that's exactly Push/Pull's job. Op file, plugin file,
  force-link entry, and CMakeLists entries all removed.
- **Toolbar `Extrude` button on `HasFaces` context + "Extrude Face"
  palette command** removed; Push/Pull covers the modify-in-place case
  and "Extrude From" covers the make-a-new-body case.

## [0.2.2] — 2026-05-29

Incremental polish on top of 0.2.1 plus the first batch of contributions
from R4stl1n landing back upstream.

### Added

- **Interactive Shell tool with thickness popup.** Picks up the existing
  `ShellOp` (hollow a body, remove the picked face) and wraps it in the
  same popup-with-live-preview pattern as push/pull and edit-diameter.
  Defaults to 1.0 mm with a slider that scrubs 0.1–20 mm; Enter / Apply
  commits, Esc reverts. Replaces the prior one-shot "always 1 mm" plugin
  button.
- **Configurable rendering settings** (from R4stl1n): the Settings panel
  grew a Rendering section with ambient-light slider, headlight (light
  follows camera) toggle, fill-light toggle, multisample anti-aliasing
  selector (Off / 2x / 4x / 8x), and a mesh-quality / OCCT-deflection
  control. Tames the harsh single-direction shadows and lets you trade
  detail for performance on heavier models.
- **Subtract sketch tool** (from R4stl1n): an extrude-cut with a red
  preview tied to the source body the sketch was drawn on. Lives on the
  sketch region toolbar so a region click can drive it directly without
  having to enter the body's face flow.
- **"Buy us a Coffee" button** in the About dialog. Proceeds are split
  between stevebushwa and R4stl1n — stevebushwa just runs the page.
  Coloured in the BMC brand yellow so it reads as a separate "support"
  action rather than another navigation button.

### Changed

- **Application.cpp slimmed by ~31 %** (2,698 → 1,851 lines). All
  interactive-op state machines (Fillet/Chamfer + Edit, Edit Diameter,
  Shell, Extrude, Push/Pull) moved into a new
  `Application_InteractiveOps.cpp`, mirroring the existing
  `Application_Viewport.cpp` / `Application_Dialogs.cpp` split. Pure
  mechanical relocation — no behaviour changes.
- **Slimmed Fillet and Chamfer plugin files** (from R4stl1n): the
  duplicate interactive paths that were shadowed by the inline app-level
  code got removed. `FilletPlugin.cpp` 165 → 56 lines,
  `ChamferPlugin.cpp` 169 → 56 lines.
- `Toolbar::renderGeneralSection` removed (dead method, defined but
  never called; its body was an intentional no-op).

## [0.2.1] — 2026-05-29

CI hotfix: the v0.2.0 Linux Build workflow couldn't execute
`scripts/build-appimage.sh` because the file lacked its executable bit in
the repo (locally I'd been invoking it through `bash`, which masked it).
Nothing else changes — this is the same code as 0.2.0 with a chmod
correction so the published AppImages actually build.

## [0.2.0] — 2026-05-29

Two new editability features on top of the 0.1.1 polish: previously-applied
fillets and chamfers are re-editable by clicking their faces, and bodies that
are essentially solid cylinders or tubes can be resized by diameter on the
cylindrical face itself. A failed attempt at fixing the sketch rotate popup
ended up making the popup disappear entirely — the underlying 15° drag-snap
still works, the type-in path is gone until the next release.

### Added

- **Edit Fillet / Edit Chamfer by clicking the face.** When the selected face
  was generated by a `FilletOp` or `ChamferOp` in the current session's
  history, a new button appears in the Face Operations toolbar. Clicking it
  re-opens the existing radius/distance editor with the value pre-filled,
  drag handle and live preview reused from the create flow, Apply re-runs
  via `History::editStep` so any downstream ops re-execute, Esc cancels.
- **Edit Diameter on any closed cylindrical face or its circular edge.**
  Pick the inner face of a hole, the outer face of a tube or solid cylinder,
  a cylindrical boss, or just one of the circular edges that bound such a
  face. Typing a new diameter live-rebuilds the body via a ring (frustum)
  boolean: the changed annular volume is fused into the body when adding
  material, cut from it when removing. Face pick edits both end edges
  together → stays a cylinder. Single-edge pick edits just that end → makes
  a cone / funnel from a previously straight feature. The resulting top and
  bottom caps are post-processed with `ShapeUpgrade_UnifySameDomain` so the
  ring's planar caps merge cleanly with the body's existing caps instead of
  leaving hairline seams. Confirm pushes a `ResizeCylindricalOp` to history;
  Esc reverts.

### Fixed

- **Sketch rotate-angle popup hover-flicker** — ported from a nested
  Begin/End floating window to a real `OpenPopup` / `BeginPopup` at top
  scope, which kills the flicker but at the cost of the popup not showing.
  See known issues.

### Known issues

- **Sketch rotate-angle popup doesn't appear after the rotate drag.** The
  15° drag snap commits as soon as you release the mouse — there's no
  type-in refinement. Likely caused by the popup's parent viewport's
  `WindowPadding=0` / `NoTitleBar` style state interacting with ImGui's
  popup focus rules. Queued to be reworked in 0.3.0 as a separate floating
  window outside the viewport scope.
- **Fillets / Chamfers / Diameter edits from reopened projects aren't
  editable.** The current save format throws away each op's parameters
  (radius, edges, etc.) and reloads them as generic snapshot replays, so
  the face-edit hooks return "no owning op." Edits work within the session
  they were created; reopening a project resets the editable parameter
  surface. Queued to be lifted in 0.3.0 by extending the save format to
  round-trip `FilletOp` / `ChamferOp` / `ResizeCylindricalOp` parameters
  alongside their result shapes.
- The carryover from 0.1.1 — push/pull ARM artifacting and ortho-view
  entry rotation — are still deferred.

## [0.1.1] — 2026-05-28

A "polish + missing-feature" release. Most work is in interaction: gizmos for
operations that were previously click-twice or hidden, live measurement readouts
during drags, and a measure tool. A handful of geometry and persistence bugs
were tracked down along the way, including one data-loss case in undo replay.

### Added

- **Measure tool** (Object / Edge / Line modes) reachable from the no-selection
  and sketch toolbars. Object measures the combined bbox of every selected body
  (clicking a face counts as picking its body); Edge sums the lengths of every
  selected edge; Line is a two-click point-to-point distance drawn as a purple
  line in the viewport that lives in 3D — orbiting moves it correctly.
- **Sketch Move/Rotate gizmo** rendered on the selection centroid in Select
  mode. Axis arrows (red X, green Y in the sketch plane) and a centre dot for
  free move snap to the active grid step; the rotate ring snaps to 15° while
  dragging. After a rotate drag, a popup lets you type an exact angle. The
  standalone sketch "Rotate" toolbar button has been retired in favour of the
  gizmo ring.
- **Sketch box-select** — click and drag from empty space in Select mode draws
  a rectangle and selects every sketch element whose screen-space projection
  lands inside. Ctrl preserves existing selection.
- **Double-click a sketch line** to select its entire connected chain (lines
  sharing endpoints, transitively). Double-clicking empty space still selects
  every element.
- **Push/Pull face arrow** — selecting a face and choosing Push/Pull now draws
  an arrow along the face normal, with a popup to type the distance and a live
  mm readout while dragging the arrow.
- **Fillet / Chamfer drag handle** on the picked edge — drag perpendicular to
  the edge to set the radius / distance, with a live measurement, alongside the
  existing value popup + slider.
- **Multi-body rotate panel** — three axis sliders + text entry + Apply/Reset/
  Close for rotating large multi-body selections. Works around the live-rotate
  gizmo being too slow on heavy selections without a renderer refactor.
- **Context-aware Ctrl+A** — nothing selected → select every body; an edge
  selected → every edge on that body; in sketch → the whole sketch.
- **3DS Max-style ViewCube** with a rotation ring around it (replaces the older
  navigation buttons).
- **Trackpad navigation mode** in Settings for laptop use.
- **Click-and-drag multi-object box select** in 3D, with Ctrl to extend.
- **Adaptive view clipping** — near/far planes scale with orbit distance so
  distant geometry and the grid no longer vanish at far zoom.
- **Sketch-face grid** now matches the host face's bounds and centroid, instead
  of always rendering a fixed 100 × 100 mm patch at the origin.
- **Grid emphasis** — every 100 lines gets a heavier weight in addition to the
  10-line emphasis.

### Changed

- **STEP import/export** now applies a Z-up → Y-up rotation around X, matching
  Materializr's world convention; export inverts the rotation so a STEP round
  trip is a no-op.
- **Move gizmo on a multi-body selection** moves every selected body, not just
  the one the gizmo is anchored to. The drag caches its pivot at start so the
  motion doesn't jolt as bodies translate beneath the renderer.
- **Sketch transforms (Copy / Mirror / Rotate)** can be invoked with nothing
  selected and will operate on the whole sketch.
- **Sketch on face** centres the face in the viewport on entry and restores the
  prior camera on exit; the camera save/restore is preserved across sketch
  enter/leave round trips.
- **Camera "up" preservation** when aligning to a sketch face via projection,
  so face-orientated views don't surprise-flip.
- **Documentation** split out of README into `docs/` (architecture, building,
  features, getting-started, usage) and the Help menu picks them up.

### Fixed

- **Critical: undo replay was deleting unselected bodies** when applied as a
  batched in-session op. `ReplayOp` now distinguishes "project reload" (where
  bodies not in the snapshot should be removed) from "in-session batch" (where
  they must not), via a `fromReload` flag.
- **Grid drifting vertically at far zoom** — the depth bias was a constant in
  NDC, so its world-space size grew with depth. Now it's a fraction of the ray
  length to the near point.
- **Sketch element drag** — clicking and holding a selected point or line
  translates the whole selection by the cursor delta each frame (was per-point
  only).
- **Sketch double-click selects everything** instead of being a no-op.
- **Multi-edge select** — Ctrl-clicking edges now extends the selection
  correctly (`SelectionManager::findEntry` matches by `IsSame()` on the shape).
- **No more jolt** when panning or orbiting during an extrude / push-pull /
  sketch placement — input projections are skipped while the camera is being
  dragged.
- **Copy offset** — duplicated sketch geometry now lands exactly on the
  original (was offset).
- **Far-zoom disappearance** of distant parts (same root cause as the grid
  drift; clip planes now adapt to orbit distance).
- **Sketch face grid** is centred on the face centroid rather than the origin.
- **Ortho view entry** retains the user's view orientation more sensibly
  (still has a known case noted below).

### Known issues

- **Sketch rotate-angle popup glitches on hover** — the small "Rotation (deg)"
  panel that appears after a rotate-gizmo drag flickers and becomes unclickable
  when the cursor is over it. The 15° drag snap works fine; only the type-in
  refinement is affected. Esc reverts and exits. To be fixed by porting the
  popup to true ImGui popup semantics (`OpenPopup` / `BeginPopup`) in a
  follow-up.
- **Push/pull multi-target preview artifacting on ARM** — visible on aarch64
  builds, not reproducible on x86_64. Deferred until a reliable repro path is
  found.
- **Ortho view entry rotates counter-clockwise** relative to expectation in
  some camera states. Exit is clean. Deferred for a separate pass.

## [0.1.0] — Initial release

Initial Materializr release. Parametric 3D CAD on OCCT with sketches, extrude,
push/pull, fillet, chamfer, transforms, plugin system, project save/load,
ViewCube, settings, Linux AppImage and Windows packaging.
