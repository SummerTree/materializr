# Getting Started

This guide walks you from a fresh download to your first solid model in about
five minutes.

## Install

### Linux (AppImage)

The AppImage is a single-file portable binary — no install, no system
dependencies beyond OpenGL drivers:

```bash
chmod +x Materializr-x86_64.AppImage
./Materializr-x86_64.AppImage
```

Grab the latest build from the
[releases page](https://github.com/materializr-cad/materializr/releases).

### Windows

Two options on the same releases page:

- **`Materializr-windows-x64.zip`** — portable. Unzip anywhere, double-click
  `materializr.exe`. Settings and projects are written to `%APPDATA%` so the
  install location can stay read-only.
- **`Materializr-Setup.exe`** — NSIS installer. Adds a Start-menu shortcut and
  an uninstaller. Choose this if you'd rather Windows manage the install.

Windows SmartScreen may warn that the publisher is unrecognised (we don't
code-sign). Click *More info → Run anyway*.

### macOS (Apple Silicon)

Download `Materializr-*-arm64.dmg` from the releases page, open it, and drag
**Materializr** to Applications. The app is Apple-Silicon only (M1 or newer)
and ad-hoc signed, not notarized — so on first launch Gatekeeper will say it
"cannot be opened because the developer cannot be verified." Right-click (or
Control-click) the app in Applications and choose **Open**, then **Open**
again in the dialog; this is a one-time approval. (Equivalently: System
Settings → Privacy & Security → **Open Anyway**.)

### Android

Designed for tablets — a phone screen will be cramped. Two sources:

- **[F-Droid](https://f-droid.org/packages/com.materializr.app/)** — installs
  and auto-updates through the F-Droid app; builds lag a few days behind.
- **APK from the releases page** (`Materializr-*-arm64-v8a.apk`) — sideload
  (enable "install unknown apps") for the freshest fixes.

Pick one source and stick with it: the two are signed with different keys, so
Android refuses to install one over the other.

## First launch: pick a layout

The first launch opens a short **Getting Started tour** that begins with a
layout picker (with live preview): **Classic** (desktop menu bar + docked
panels), **Modern** (top app bar + tool rail), or **Im-Touch** (full-bleed
viewport with floating overlays, built for touch). You can switch any time in
Settings → Appearance, and re-run the tour from **Help → Getting Started**.

## Your first model

The steps below use the **Classic** layout; the same tools exist in every
layout (the tour shows where they live in yours). The default workspace shows
a 20 mm demo cube and three panels: the **Tools** toolbar on the left,
**Items** + **History** + **Properties** on the right, and a status bar at
the bottom. The cube is purely decorative — feel free to delete it from the
Items panel (press <kbd>Delete</kbd> with the body selected).

### 1. Start a sketch

With nothing selected, click **Sketch on XY** in the toolbar. The camera snaps
to a top-down orthographic view aligned to the world XY plane. A face-on grid
appears.

### 2. Draw a rectangle

Click **Rectangle**, then click two opposite corners on the grid. Or type a
size (e.g. `40`) right after the first click to lock the rectangle to that
side length in the direction of the cursor.

### 3. Finish the sketch

Click **Finish Sketch** (or press <kbd>Enter</kbd>). The sketch is saved into
the Items panel and is now selectable like any other geometry.

### 4. Extrude

Hover inside the rectangle until the region highlights cyan. Click it to
select. Then click **Push / Pull** in the toolbar. A green arrow appears
sticking out of the region — drag it (or type a value in the popup) to set the
height. Press <kbd>Enter</kbd> to confirm.

You now have a solid box.

### 5. Add a fillet

Click near one of the top edges (the cursor turns the edge green when it's
within 8 px). Click **Fillet**. A handle appears pointing outward from the
edge — drag it away from the edge to grow the radius (starts at 0.1 mm), or
type a value. <kbd>Enter</kbd> confirms.

### 6. Save

<kbd>Ctrl</kbd>+<kbd>S</kbd> → pick a name → done. The project file (`.materializr`) stores the
final geometry plus the operation history so you can reopen and continue.

## Where to go next

- **[features.md](features.md)** — what every tool does.
- **[usage.md](usage.md)** — workflow recipes (boolean ops, gizmos, sketches on
  faces, keyboard shortcuts).
- The in-app **Help → User Guide** is a condensed version of this guide.
