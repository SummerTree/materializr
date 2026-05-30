# Usage Guide

Workflow recipes for common tasks, plus the full keyboard-shortcut table. For a
guided first-model walkthrough, see [getting-started.md](getting-started.md).

## Basic workflow

1. With nothing selected, click **Sketch on XY / XZ / YZ** to sketch on a base
   plane — or select a face first.
2. Click **Sketch on Face** (when a face is selected) — the camera snaps to an
   orthographic view straight at the face.
3. Draw a closed profile (Rectangle, Circle, chain of Lines, …). Type
   exact dimensions during placement, or rely on grid snap.
4. Click **Finish Sketch** — the sketch is saved to the Items panel.
5. **Hover** an enclosed region of the sketch in the viewport; it
   highlights cyan. Click to select, Ctrl+click to add more regions.
6. Click **Push / Pull** — drag positive to extrude, negative to cut.

## Sketch on a face

1. Click any planar face on a body.
2. Click **Sketch on Face** in the toolbar, or right-click → *Sketch on this Face*.
3. Draw on the face — the world grid extends across to neighbouring faces so
   you can reference adjacent geometry.
4. Click **Finish Sketch** when done — the sketch persists in the Items panel.
5. Re-enter it later via **Edit Sketch** to add concentric circles, holes, etc.

## Push / Pull a region

1. Select one or more closed regions of a finished sketch
   (hover → click; Ctrl+click for multi).
2. Click **Push / Pull**.
3. Drag the arrow along the face normal, or type a distance:
   - **Positive** = extrude outward, fused with the sketch's source body
     (or as a new body if free-floating).
   - **Negative** = cut into the source body.
4. **Enter** to confirm, **Escape** to cancel.

## Fillet / Chamfer

1. **Click near an edge** (within ~8 px) — edge highlights in green.
2. **Ctrl+click** more edges to add to the selection.
3. Click **Fillet** or **Chamfer** in the toolbar.
4. **Drag the outward handle** away from the edge to set the radius/distance
   (from 0.1 mm, with a measurement), or type a value / use the slider — live
   preview updates.
5. **Enter** to confirm, **Escape** to cancel.

## Transform with the gizmo

1. Double-click a body — gizmo appears. Multi-select multiple bodies first if
   you want to move them together.
2. Drag **arrows** to move, **rings** to rotate, **cubes** to scale.
3. With **Snap to grid** on, the translate axes snap to the current grid
   step (0.1 / 0.5 / 1 / 10 mm).
4. Release mouse — transform commits to history.
5. **Escape mid-drag** restores the body to where it was before the drag.

Move applies the same translation to every selected body. Rotate and Scale
operate on the primary (first-selected) body for now.

## Editing a sketch

1. Click **Edit Sketch** with a sketch (or one of its regions) selected — or
   double-click the sketch row in the Items panel.
2. The sketch opens in **Select / Move** mode by default. Click a point or
   line to select it; Ctrl+click to add more; double-click empty space to
   select the entire sketch.
3. Drag a selected element to translate the whole selection. Endpoints of
   selected lines come along automatically.
4. **Copy / Mirror / Rotate** buttons in the toolbar act on the selection
   (or the whole sketch if nothing is selected):
   - **Copy** duplicates the selection in place — the new elements become
     selected so you can immediately drag them to position.
   - **Mirror** flips horizontally across the selection's centroid.
   - **Rotate** enters an interactive drag mode — moving the cursor around
     the centroid rotates the selection; click to commit, Esc to cancel.
5. Click **Finish Sketch** when done.

## Boolean operations

1. **Ctrl+click** a face on body A, then **Ctrl+click** a face on body B.
2. Click **Union**, **Subtract**, or **Intersect**.
3. Unions auto-merge coplanar/cotangent neighbouring faces so the result
   has no spurious seam edge between the original bodies.

## Re-editing a fillet or chamfer

Click the rounded face of the fillet (or the flat chamfer face) in the
viewport. The history panel opens that step in its inline editor; change the
radius/distance and click **Apply Changes**. The op replays in place — the
base geometry and any later steps follow.

## Navigation

Defaults (the orbit/pan buttons are reassignable in File → Settings):

| Input | Action |
|-------|--------|
| Middle mouse drag | Orbit (exits sketch ortho lock) |
| Shift + Middle mouse | Pan (preserves ortho) |
| Right mouse drag | Pan |
| Scroll wheel | Zoom (preserves ortho) |
| Home | Reset camera |
| ViewCube face | Snap to orthographic view |
| ViewCube corner dot | Snap to isometric view |
| ViewCube side arrow | 90° camera rotation |
| ViewCube body drag | Free orbit (direction invertible in Settings) |

**Trackpad mode** (Settings → *Trackpad mode*) maps orbit + pan onto the left
mouse button using Shift as the modifier — useful on laptops without a middle
button.

## Updates

**Help → Check for Updates** queries the GitHub releases API for the latest
tag and tells you whether you're up to date. If a newer release exists, the
**Open Release Page** button takes you to the download.

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+S | Save Project |
| Ctrl+O | Open Project |
| Ctrl+I | Import STEP |
| Ctrl+E | Export STEP |
| Ctrl+C | Copy |
| Ctrl+D | Duplicate |
| Delete | Delete Selected |
| Escape | Cancel / revert in-progress drag / exit sketch |
| Enter | Confirm Push&nbsp;Pull / Extrude / Fillet / Chamfer |
| Home | Reset Camera |
| W | Gizmo: Translate mode |
| E | Gizmo: Rotate mode |
| R | Gizmo: Scale mode |

## Troubleshooting

### Recovering from a crash caused by rendering settings

The rendering controls in **File → Settings → Rendering** (ambient, headlight
/ fill light, MSAA samples, mesh quality) are persisted across launches.
That's normally what you want, but it means a setting that crashes your GPU
or driver will keep crashing the app on every launch.

On some hardware setups — particularly older Intel iGPUs, virtualized GPUs
inside VMs, and some Linux distro + driver combinations — turning **MSAA**
or **Mesh quality** all the way up triggers a driver crash. The app brings
those back up on the next launch and crashes again.

#### Easiest: launch with `--safe-mode`

From a terminal (or by editing your launcher / shortcut to append the flag):

```bash
./Materializr-x86_64.AppImage --safe-mode    # Linux
materializr.exe --safe-mode                  # Windows
```

`--safe-mode` (aliases: `--safe-graphics`, `--low-graphics`) loads with a
known-safe configuration: **MSAA off, mesh quality Low, default lights,
autosave off, auto-open-last-project off** — and writes those values to the
settings file so the next normal launch stays recovered. Use it if a
previously-saved setting crashes the app at startup, or if a complex
auto-opened project hangs a lower-core machine. From there you can turn
things back up gradually via Settings and stop wherever your hardware is
happy.

`--help` (or `-h`) prints the available CLI options.

#### Alternative: reset the settings file by hand

If you'd rather wipe everything and start fresh, delete the settings file:

- **Linux**: `~/.config/materializr/settings.cfg`
- **Windows**: `%APPDATA%\materializr\settings.cfg`

Materializr writes a fresh file with defaults on the next launch
(`MSAA = 4`, `Mesh quality = Medium`, ambient `0.40`, headlight off, fill on).

If you only want to reset rendering and keep everything else, open the
settings file in a text editor and either delete the offending key
(`msaaSamples`, `meshQuality`, `lightAmbient`, `lightHeadlight`, `lightFill`)
or set it back to its default. Unknown keys are ignored on read and the
defaults take over.
