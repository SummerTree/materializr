# Architecture

A high-level tour of how the codebase is organised and the patterns it leans
on. Aimed at someone about to dig in and make changes.

## Source layout

```
src/
├── app/          # Application lifecycle, main loop, input handling
├── core/         # Document, History, Operation, Selection, EventBus, Material
├── modeling/     # CAD operations (Extrude, PushPull, Fillet, Boolean, Sketch, …)
├── plugin/       # Plugin registry + contribution types (toolbar/command/IO/tool)
├── plugins/      # Each operation, tool, and IO format registered as a plugin
├── viewport/     # 3D rendering (Camera, Grid, ShapeRenderer, Gizmo, ViewCube, Picker)
├── ui/           # ImGui panels (Toolbar, History, Items, Properties, …)
└── io/           # File I/O (STEP, STL, IGES, glTF, ProjectIO, Settings, FileDialogs)
shaders/          # GLSL shaders (grid, mesh, outline)
tests/            # Google Test unit tests
scripts/          # Build scripts (AppImage packaging)
packaging/        # NSIS installer script for Windows
.github/          # CI workflows
```

## Plugin registry

Modelling operations, interactive tools, and IO formats are registered through
a **plugin registry** (`src/plugin`). Each feature in `src/plugins` declares
its toolbar buttons, commands, and handlers in a `REGISTER_PLUGIN(...)` block
that runs at startup. The adaptive toolbar surfaces those buttons based on
the current selection context (nothing / face / edge / body / sketch / sketch
region).

This means adding a new modelling op is mostly a matter of dropping a new
`*Plugin.cpp` next to the others and listing it in `CMakeLists.txt`; the rest
of the app discovers it automatically.

A few interactive tools (the gizmo-based ones, push/pull, fillet/chamfer) live
in `Application.cpp` rather than as plugins because they need viewport drag
events that the plugin-tool interface doesn't expose.

## Core design patterns

- **Command pattern** — every modelling operation derives from `Operation`
  (`src/core/Operation.h`) and stores enough state to `undo()` itself. Push
  /Pull, fillet, chamfer, extrude, delete, transform, mirror, split — all
  undoable.
- **Linear history** — `History` (`src/core/History.cpp`) maintains an ordered
  list of operations. The model is the result of replaying them. `editStep`
  walks back to a step in place (via per-op `undo()` then re-`execute()`)
  rather than clearing and replaying from scratch, so the base bodies and
  body ids stay stable.
- **`captureDiff()` for history persistence** — each op reports the set of
  modified/created/deleted body shapes it produced (read from its stored
  undo data, not via `undo()`/`execute()`). A reverse walk over those diffs
  reconstructs the body state after each step, which is what the project file
  stores. On reload the history is rebuilt as `ReplayOp` instances that
  restore the right body set per step.
- **Adaptive UI** — toolbar content changes based on selection type
  (nothing, face, edge, body, sketch, sketch region). Implemented as a
  cascade of `renderXxxTools()` methods in `Toolbar.cpp`.
- **Interactive preview** — push/pull, extrude, fillet, chamfer, the gizmo,
  and the sketch rotate mode all show live results as you type or drag, then
  commit to history on confirm. Escape during the drag reverts to the state
  before the operation started.
- **Separation of concerns** — geometry kernel (OCCT), UI (ImGui), and
  rendering (OpenGL) are kept apart. `core/` and `modeling/` know nothing
  about ImGui or OpenGL; `ui/` and `viewport/` know nothing about the
  modelling operations beyond their public API.

## Tech stack

| Component | Technology |
|-----------|-----------|
| Geometry kernel | OpenCASCADE Technology (OCCT) — 7.9 on Linux, 8.0 on Windows via vcpkg |
| UI framework | Dear ImGui (docking branch) |
| Windowing | GLFW 3.4 |
| Rendering | OpenGL 3.3 Core, PBR shading |
| GL loader | Mesa prototypes on Linux, GLEW on Windows |
| Math | GLM |
| HTTP | libcurl (Help → Check for Updates) |
| Build | CMake 3.20+ |
| Packaging | Docker + AppImage (Linux), CPack/NSIS (Windows) |
| Testing | Google Test |
| Language | C++17 |

## Cross-platform notes

The codebase is kept platform-portable with localised `#ifdef _WIN32` blocks
rather than parallel implementations:

- **GL loader** (`src/gl_common.h`) — Mesa prototypes on Linux, GLEW on
  Windows.
- **File dialogs** (`src/io/FileDialogs.cpp`) — POSIX `dirent`/`stat` on
  Linux, `std::filesystem` on Windows.
- **Settings path** (`src/io/Settings.cpp`) — `$XDG_CONFIG_HOME` / `~/.config`
  on Linux, `%APPDATA%` on Windows.
- **ImGui layout file** — relative `imgui.ini` on Linux (AppImage runs from a
  writable cwd), `%APPDATA%\materializr\imgui.ini` on Windows (Program Files
  isn't writable for non-admin processes).
- **OpenCASCADE discovery** (`CMakeLists.txt`) — Linux patches the system
  libtbb-symlink mismatch; Windows uses vcpkg's CMake config.
