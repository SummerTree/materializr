# Materializr

Open-source parametric 3D CAD

Materializr is a desktop CAD application built with OpenCASCADE Technology for
solid modeling, Dear ImGui for the interface, and OpenGL for rendering. It
provides a linear parametric design history, a 2D constraint-based sketcher,
unified push/pull modeling, and a broad set of operations in a single
lightweight binary.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-blue)
![License](https://img.shields.io/badge/License-MIT-green)

## Quick Start (AppImage)

Pre-built AppImage — no install, no dependencies:

```bash
chmod +x Materializr-aarch64.AppImage
./Materializr-aarch64.AppImage
```

To build the AppImage yourself:

```bash
./scripts/build-appimage.sh
# Output: dist/Materializr-<arch>.AppImage
```

Requires Docker with BuildKit enabled.

## Features

### Modeling Operations

- **Push / Pull** — unified extrude/cut. Drag a positive distance to add
  material, negative to cut into the source body. Works on sketch regions
  *and* body faces. Unions auto-merge coplanar/cotangent faces so seams
  disappear from the merged result.
- **Extrude** — interactive extrude with live preview, draft angle,
  boolean modes
- **Revolve** — profile around an axis, 0–360°
- **Sweep** — profile along a path curve
- **Loft** — through multiple cross-section profiles
- **Fillet** — interactive radius input with live preview
- **Chamfer** — interactive distance input with live preview
- **Shell** — hollow a solid with uniform wall thickness
- **Offset Face** — push or pull individual faces
- **Split X / Y / Z** — divide the selected body with a plane through its
  bounding-box centre, perpendicular to the chosen axis
- **Boolean** — Union, Subtract, Intersect (Ctrl+click two bodies); unions
  are post-processed with `ShapeUpgrade_UnifySameDomain` so smooth merges
  don't leave a seam edge between the original bodies
- **Move** — via the 3D translate gizmo with optional grid snap
- **Rotate / Scale** — via the 3-axis gizmo
- **Mirror X / Y / Z** — reflect the body across its own bounding plane on the
  chosen axis (the copy lands flush beside it); **Mirror across Face** reflects
  across a selected planar face
- **Linear Pattern** — repeat along a direction
- **Radial Pattern** — repeat around an axis
- **Copy / Duplicate** — clone selected bodies
- **Delete** — remove bodies (undoable, including from the Items panel)
- **Align** — point-to-point snap
- **Construction Planes** — custom reference planes for sketching

### 2D Sketching

- **Tools**: Line, Circle, Rectangle, Arc, Spline, Polygon, Text
- **Sketch from scratch on a base plane** — with nothing selected, **Sketch on
  XY / XZ / YZ** starts a sketch on that base plane with the matching standard
  view (Top / Front / Right); no body required
- **Sketch on any planar face** — toolbar button when a face is selected
  or right-click → *Sketch on this Face*; the sketch's plane is taken
  directly from the face
- **Persistent sketches** — finishing a sketch saves it into the document's
  Items panel; it doesn't auto-extrude. Select it (or a region of it) later to
  **Edit Sketch** (continue drawing) or **Push/Pull** an enclosed region
- **General region detection** — the sketch's geometry is partitioned into
  atomic planar regions using the boolean engine, so *intersecting and
  overlapping shapes* (the lens between two circles, an annulus, the cells
  on either side of an open dividing line) each become individually
  selectable — not just simple closed loops
- **Region hover & multi-select** — hover a region in the viewport to
  highlight it; click to select, Ctrl+click to add more regions to a
  push/pull operation. A wider catch area around the boundary lines makes
  thin regions easy to grab
- **Manifold extrusion** — outer wire + inner wires get assembled into a
  single planar face with holes (e.g. a sketched ring extrudes into a tube)
- **Inline dimension input** — while placing a Line / Circle / Polygon /
  Rectangle, type the length, radius, or side directly; the value is
  applied from the click anchor toward the cursor direction
- **Live dimension overlay** — a measuring annotation (offset dimension line
  with arrowheads + value) follows the operation as you work: line length,
  circle **diameter**, both rectangle sides while sketching; depth while
  extruding; distance while moving a body with the gizmo
- **Snap** — to existing sketch points, to circle/arc perimeters, and
  threshold-based to grid lines (snaps only when close)
- **Adjustable grid step** — pick 0.1, 0.5, 1, or 10 mm; the same step
  drives the visual grid, sketch snap, and gizmo translate snap. Every 10th
  grid line is drawn brighter so larger distances are easy to read
- **Plane-aligned grid** — a base-plane sketch shows the world grid laid on
  the sketch plane (and visible in the orthographic sketch view); a sketch on
  a face shows a face-local measurement grid in the sketch's own 2D axes
- **Concentric / shared anchors** — clicking on an existing point in
  Circle / Polygon / Arc mode reuses that point so concentric shapes
  actually share a center vertex
- **Constraints**: Coincident, Horizontal, Vertical, Distance, Radius,
  Parallel, Perpendicular, Tangent, Equal, Concentric
- **Auto-constraining** — snaps to horizontal/vertical when lines are
  near-aligned

### Camera / View

- **Auto-orthographic on sketch entry** — when you start a sketch, the
  camera snaps to an orthographic view looking straight down the sketch's
  plane normal, framed to the source face
- **Pan and zoom preserve ortho**; **orbit** exits ortho back to perspective
- **"Look at Sketch"** button — appears in the sketch toolbar only when
  the camera isn't ortho, returns to the aligned ortho view
- **ViewCube** — Top / Bottom / Front / Back / Left / Right standard views,
  plus four rotate buttons (15° increments). Any ViewCube action exits the
  sketch ortho lock and re-frames the visible geometry

### Selection System

| Action | Selects |
|--------|---------|
| Click near edge | Edge (for Fillet/Chamfer) |
| Click on face | Face (for Extrude / Push&nbsp;Pull / Sketch on Face) |
| Click on sketch region | SketchRegion (for Push/Pull) |
| Double-click | Whole body (shows transform gizmo) |
| Ctrl+click | Add to multi-selection (faces, regions, edges, bodies) |
| Right-click face | Context menu (Sketch on Face, Extrude, Select Body) |
| Click empty space | Clear selection |

Selection is **occlusion-aware**: edges and sketch regions hidden behind a
solid can't be picked through it — only what's visible is selectable.

### Interactive Tools

All major operations provide **live preview** as you adjust values:

- **Push/Pull**: positive distance extrudes outward, negative cuts inward.
  Works on selected sketch regions and on body faces. Multi-region in one op.
- **Extrude**: drag in viewport or type distance (mm), slider, Enter to confirm
- **Fillet**: type radius or use slider (0.1–20 mm), live preview on edges
- **Chamfer**: type distance or use slider, live preview
- **Move (Gizmo)**: 3-axis arrows with threshold-based grid snap; the body
  jumps in step-sized increments around the snap point and is otherwise free
- **Rotate / Scale (Gizmo)**: rings and cubes on the same gizmo
- **Cancel mid-drag with Escape** — pressing Escape while still dragging
  a gizmo, an extrude, a push/pull, a fillet/chamfer, or a sketch tool
  reverts the body / cancels the preview so the model returns to exactly
  where it started

### Rendering

- PBR Cook-Torrance BRDF shading with tone mapping
- 12 material presets (Steel, Aluminum, Copper, Gold, Plastics, Wood,
  Glass, Rubber, Ceramic, Concrete)
- Gradient background
- Edge wireframe overlay
- Per-face / per-edge / per-body / per-region selection highlighting
  (outline only, no z-fighting)
- Infinite grid with red X-axis and blue Z-axis
- Construction plane visualization

### UI / UX

- **Adaptive toolbar** — shows relevant tools based on selection
  (nothing / edge / face / body / sketch / sketch region)
- **Design History** — every operation recorded, undo/redo (Ctrl+Z / Y),
  breakpoints. Includes Push/Pull, deletes, gizmo moves, and sketch ops
- **Items panel** — bodies *and* sketches with visibility, rename (double-click
  or right-click), and delete (Delete key or right-click). Body deletes are
  undoable.
- **Interactions panel** — a live reference of the viewport controls
  (camera / select / transform / sketch), docked above the Items panel; the
  camera rows reflect the current mouse bindings
- **Properties panel** — edit any operation's parameters after creation
- **Command Palette** (Ctrl+K) — fuzzy search all commands
- **Material panel** — assign PBR materials to bodies
- **Measure tool** — distance, area, edge length, bounding box
- **2D Drawing workspace** — Front/Top/Right/Isometric projections with
  DXF/SVG export
- **Section View** — cut through solids with interactive clipping plane
- **Transform Gizmo** — 3-axis arrows + rotation rings + scale cubes
- **Box / Marquee selection**
- **Dark / Light themes**
- **Status bar** — body count, selection info, current tool
- **Variables & Expressions** — named parameters (e.g. `width=50`,
  `height=width*0.6`)
- **Version snapshots** — auto-save + manual save with labels, restore
  any version
- **Settings** (File → Settings) — reassign which mouse buttons orbit and pan
- **Toast notifications**, **Keyboard Shortcuts panel**, **About dialog**

### File I/O

| Format | Import | Export |
|--------|:------:|:------:|
| Native `.materializr` project | yes | yes |
| STEP (.step / .stp) | yes | yes |
| IGES (.iges / .igs) | yes | yes |
| STL (.stl) | — | yes |
| DXF (.dxf, 2D drawings) | — | yes |
| SVG (.svg, 2D drawings) | — | yes |
| glTF / GLB (.glb) | — | yes |
| TGA / PPM screenshot | — | yes |

## Building from Source

### Prerequisites (Ubuntu / Debian)

```bash
sudo apt-get install -y \
  build-essential cmake git \
  libocct-data-exchange-dev \
  libocct-draw-dev \
  libocct-foundation-dev \
  libocct-modeling-algorithms-dev \
  libocct-modeling-data-dev \
  libocct-visualization-dev \
  libgl-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxi-dev libxkbcommon-dev \
  libwayland-dev pkg-config
```

### Build

```bash
git clone https://github.com/user/materializr.git
cd materializr
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Run

```bash
./build/materializr
```

### Test

```bash
cd build
ctest --output-on-failure
```

### Build AppImage (Docker)

Produces a portable single-file Linux executable:

```bash
./scripts/build-appimage.sh
# Output: dist/Materializr-<arch>.AppImage
```

Requires Docker with BuildKit. The AppImage bundles OCCT, TBB, and Freetype —
the user only needs base OpenGL/X11 drivers.

## Usage

### Basic Workflow

1. With nothing selected, click **Sketch on XY / XZ / YZ** to sketch on a base
   plane — or select a face first
2. Click **Sketch on Face** (when a face is selected) — the camera snaps to an
   orthographic view straight at the face
3. Draw a closed profile (Rectangle, Circle, chain of Lines, …). Type
   exact dimensions during placement, or rely on grid snap
4. Click **Finish Sketch** — the sketch is saved to the Items panel
5. **Hover** an enclosed region of the sketch in the viewport; it
   highlights cyan. Click to select, Ctrl+click to add more regions
6. Click **Push / Pull** — drag positive to extrude, negative to cut

### Sketch on a Face

1. Click any planar face on a body
2. Click **Sketch on Face** in the toolbar, or right-click → *Sketch on this Face*
3. Draw on the face — sketch renders directly on the surface with a
   face-local measurement grid
4. Click **Finish Sketch** when done — the sketch persists in the Items panel
5. Re-enter it later via **Edit Sketch** to add concentric circles, holes, etc.

### Push / Pull a Region

1. Select one or more closed regions of a finished sketch
   (hover → click; Ctrl+click for multi)
2. Click **Push / Pull**
3. Type a distance or use the slider:
   - **Positive** = extrude outward, fused with the sketch's source body
     (or as a new body if free-floating)
   - **Negative** = cut into the source body
4. **Enter** to confirm, **Escape** to cancel

### Fillet / Chamfer

1. **Click near an edge** (within ~8 px) — edge highlights in green
2. **Ctrl+click** more edges to add to selection
3. Click **Fillet** or **Chamfer** in the toolbar
4. Type radius/distance or drag the slider — live preview updates
5. **Enter** to confirm, **Escape** to cancel

### Transform with Gizmo

1. Double-click a body — gizmo appears
2. Drag **arrows** to move, **rings** to rotate, **cubes** to scale
3. With **Snap to grid** on, the translate axes snap to the current grid
   step (0.1 / 0.5 / 1 / 10 mm)
4. Release mouse — transform commits to history
5. **Escape mid-drag** restores the body to where it was before the drag

### Boolean Operations

1. **Ctrl+click** a face on body A, then **Ctrl+click** a face on body B
2. Click **Union**, **Subtract**, or **Intersect**
3. Unions auto-merge coplanar/cotangent neighbouring faces so the result
   has no spurious seam edge between the original bodies

### Navigation

Defaults (the orbit/pan buttons are reassignable in File → Settings):

| Input | Action |
|-------|--------|
| Middle mouse drag | Orbit (exits sketch ortho lock) |
| Shift + Middle mouse | Pan (preserves ortho) |
| Right mouse drag | Pan |
| Scroll wheel | Zoom (preserves ortho) |
| Home | Reset camera |
| ViewCube | Click standard views, or use the rotate buttons (15°) |

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+S | Save Project |
| Ctrl+O | Open Project |
| Ctrl+I | Import STEP |
| Ctrl+E | Export STEP |
| Ctrl+K | Command Palette |
| Ctrl+C | Copy |
| Ctrl+D | Duplicate |
| Delete | Delete Selected |
| Escape | Cancel / revert in-progress drag / exit sketch |
| Enter | Confirm Push&nbsp;Pull / Extrude / Fillet / Chamfer |
| Home | Reset Camera |
| W | Gizmo: Translate mode |
| E | Gizmo: Rotate mode |
| R | Gizmo: Scale mode |

## Architecture

```
src/
├── app/          # Application lifecycle, main loop, input handling
├── core/         # Document, History, Operation, Selection, EventBus, Material
├── modeling/     # CAD operations (Extrude, PushPull, Fillet, Boolean, Sketch, …)
├── plugin/       # Plugin registry + contribution types (toolbar/command/IO/tool)
├── plugins/      # Each operation, tool, and IO format registered as a plugin
├── viewport/     # 3D rendering (Camera, Grid, ShapeRenderer, Gizmo, ViewCube, Picker)
├── ui/           # ImGui panels (Toolbar, History, Items, CommandPalette, …)
└── io/           # File I/O (STEP, STL, IGES, glTF, Project save/load)
shaders/          # GLSL shaders (grid, mesh, outline)
tests/            # Google Test unit tests
scripts/          # Build scripts (AppImage packaging)
```

The modeling operations, interactive tools, and IO formats are contributed
through a **plugin registry** (`src/plugin`): each feature in `src/plugins`
registers its toolbar buttons, commands, and handlers at startup, and the
adaptive toolbar surfaces them based on the current selection context.

**Key design patterns:**

- **Command Pattern** — every modeling operation is an undoable command
  stored in the design history. Push/Pull, fillet, chamfer, extrude,
  delete, transform — all undoable.
- **Linear History** — operations form a timeline that can be replayed,
  edited, or rolled back. Edit a past step and everything after replays.
- **Adaptive UI** — toolbar content changes based on selection type
  (nothing, face, edge, body, sketch, sketch region).
- **Interactive Preview** — push/pull, extrude, fillet, chamfer, and the
  transform gizmo all show live results as you type or drag, then commit
  to history on confirm. Escape during the drag reverts to the state
  before the operation started.
- **Separation of Concerns** — geometry kernel (OCCT), UI (ImGui), and
  rendering (OpenGL) are cleanly separated.

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Geometry Kernel | OpenCASCADE Technology (OCCT) |
| UI Framework | Dear ImGui (docking branch) |
| Windowing | GLFW 3.4 |
| Rendering | OpenGL 3.3 Core, PBR shading |
| Math | GLM |
| Build | CMake 3.20+ |
| Packaging | Docker + AppImage |
| Testing | Google Test |
| Language | C++17 |

## License

MIT — see [LICENSE](LICENSE).

## Contributing

Contributions welcome. Please open an issue first to discuss changes.

## Acknowledgments

- [OpenCASCADE Technology](https://dev.opencascade.org/) — geometry kernel
- [Dear ImGui](https://github.com/ocornut/imgui) — immediate-mode GUI
- [GLFW](https://www.glfw.org/) — windowing and input
- [GLM](https://github.com/g-truc/glm) — math library
