#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <glm/glm.hpp>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>

namespace materializr {

class Window;
class Viewport;
class Grid;
class ShapeRenderer;
class SketchRenderer;
class EdgeRenderer;
class PlaneRenderer;
class BackgroundRenderer;
class ViewCube;
class Picker;
class Gizmo;
class SelectionHighlight;
class Toolbar;
class HistoryPanel;
class ItemsPanel;
class CommandPalette;
class StatusBar;
class ThemeManager;
class PropertiesPanel;
class Sketch;
class SketchSolver;
class SketchTool;
class EventBus;
class PluginContext;

} // namespace materializr

class Document;
class History;
class SelectionManager;

namespace materializr {

class Application {
public:
    Application();
    ~Application();

    void run();

private:
    void initImGui();
    void shutdownImGui();
    void initRenderers();
    void setupCommands();
    void beginFrame();
    void endFrame();
    void renderDockspace();
    void renderViewport();
    void renderMenuBar();
    void renderInteractionsPanel();
    void renderSettings();
    void handleToolAction(int action);
    void handleShortcuts();
    void handleViewCubeAction(int action);
    void rebuildMeshes();
    glm::vec2 screenToSketch(float sx, float sy, float vpW, float vpH);

    void importStepFile();
    void exportStepFile();
    void exportStlFile();
    void saveProject();         // Save dialog (Save As behavior)
    void saveProjectQuick();    // Save to current path if known, else falls through to saveProject
    void loadProject();

    // Dirty tracking + unsaved-changes prompt
    bool isDirty() const;
    void markDirty();           // for changes that don't go through History
    void markSaved();
    void renderSavePrompt();
    void requestClose();        // called when the user clicks the window X

    void enterSketchMode();
    void enterSketchOnPlane(const gp_Pln& plane);
    void enterSketchOnFace(const TopoDS_Face& face);
    void editSketch(int sketchId);
    void extrudeSketchById(int sketchId);
    TopoDS_Face buildSketchProfileFace(const Sketch& sketch) const;
    void exitSketchMode();

    // Snapshot the active sketch, run `mutator`, and if the element count
    // changed, push a SketchEditOp so the user can Ctrl+Z drawing actions.
    void recordSketchMutation(const std::function<void()>& mutator);

    // Align the orbit camera to look straight at the active sketch's plane in ortho.
    // Called when entering sketch mode / editing an existing sketch.
    void alignCameraToActiveSketch();

    // Sketch region hover/pick + Push/Pull
    struct SketchRegionHit { int sketchId = -1; int regionIndex = -1; glm::vec3 worldPoint{0.0f}; };
    SketchRegionHit pickSketchRegion(float screenX, float screenY,
                                     float vpW, float vpH) const;
    void beginPushPull();
    void updatePushPull();
    void commitPushPull();
    void cancelPushPull();
    void beginInteractiveExtrude(const TopoDS_Shape& profile);
    void updateInteractiveExtrude();
    void commitInteractiveExtrude();
    void cancelInteractiveExtrude();

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Viewport> m_viewport;
    std::unique_ptr<Grid> m_grid;
    std::unique_ptr<ShapeRenderer> m_shapeRenderer;
    std::unique_ptr<SketchRenderer> m_sketchRenderer;
    std::unique_ptr<EdgeRenderer> m_edgeRenderer;
    std::unique_ptr<PlaneRenderer> m_planeRenderer;
    std::unique_ptr<BackgroundRenderer> m_backgroundRenderer;
    std::unique_ptr<ViewCube> m_viewCube;
    std::unique_ptr<Picker> m_picker;
    std::unique_ptr<Gizmo> m_gizmo;
    std::unique_ptr<SelectionHighlight> m_selectionHighlight;
    std::unique_ptr<Document> m_document;
    std::unique_ptr<History> m_history;
    std::unique_ptr<SelectionManager> m_selection;
    std::unique_ptr<EventBus> m_eventBus;
    std::unique_ptr<PluginContext> m_pluginContext;

    // UI panels
    std::unique_ptr<Toolbar> m_toolbar;
    std::unique_ptr<HistoryPanel> m_historyPanel;
    std::unique_ptr<ItemsPanel> m_itemsPanel;
    std::unique_ptr<CommandPalette> m_commandPalette;
    std::unique_ptr<StatusBar> m_statusBar;
    std::unique_ptr<ThemeManager> m_themeManager;
    std::unique_ptr<PropertiesPanel> m_propertiesPanel;

    // Sketch
    std::shared_ptr<Sketch> m_activeSketch;
    std::unique_ptr<SketchSolver> m_sketchSolver;
    std::unique_ptr<SketchTool> m_sketchTool;
    bool m_inSketchMode = false;
    int m_activeSketchId = -1; // document id of the sketch being edited, or -1 if new

    // Hovered sketch region (for highlight in viewport)
    int m_hoveredSketchId = -1;
    int m_hoveredRegionIndex = -1;

    // Numeric dimension input shown while placing a sketch shape
    char m_sketchDimBuf[32] = "";
    bool m_sketchDimWasShown = false; // tracks placing transitions to grab keyboard focus

    // Sketch grid step in mm (drives both the visual face grid and snap-to-line)
    float m_sketchGridStep = 1.0f;

    // Push/Pull interactive operation state
    bool m_pushPullActive = false;
    bool m_pushPullPreviewPushed = false; // true while a preview PushPullOp is on the history stack
    float m_pushPullDistance = 5.0f;
    char m_pushPullInputBuf[32] = "5.0";
    bool m_pushPullInputFocus = true;

    // Snap-to-grid for gizmo translate (shares the grid step with the sketch grid).
    bool m_snapToGrid = true;

    // Configurable camera mouse bindings (ImGuiMouseButton values: 0=Left,1=Right,
    // 2=Middle). Zoom is always the scroll wheel. Edited in File > Settings.
    int m_orbitButton = 2; // Middle
    int m_panButton = 1;   // Right
    bool m_showSettings = false;
    int m_settingsOrbitButton = 2; // staged value in the Settings dialog
    int m_settingsPanButton = 1;
    // Each entry: a separate region operation to perform on commit
    struct PushPullTarget {
        int sketchId;
        int regionIndex;
        int sourceBodyId; // -1 for floating (NewBody)
        TopoDS_Face profile;
    };
    std::vector<PushPullTarget> m_pushPullTargets;
    std::vector<int> m_pushPullPreviewBodyIds;
    // For undoing previews
    std::vector<std::pair<int, TopoDS_Shape>> m_pushPullPreviousBodies;

    bool m_renderersReady = false;
    bool m_meshesDirty = true;
    int m_hoveredBodyId = -1;

    // Gizmo drag state for history commit
    bool m_gizmoDragging = false;
    int m_gizmoDragBodyId = -1;
    TopoDS_Shape m_gizmoDragOriginalShape;
    // Accumulated delta from drag start (translate only). Used so snap-to-grid
    // can snap the absolute position rather than each per-frame increment.
    glm::vec3 m_gizmoTotalDelta{0.0f};

    // Interactive fillet/chamfer state
    enum class EdgeOpType { None, Fillet, Chamfer };
    EdgeOpType m_edgeOpType = EdgeOpType::None;
    bool m_edgeOpActive = false;
    int m_edgeOpBodyId = -1;
    std::vector<TopoDS_Shape> m_edgeOpEdges;
    float m_edgeOpValue = 1.0f;
    char m_edgeOpInputBuf[32] = "1.0";
    bool m_edgeOpInputFocus = true;
    TopoDS_Shape m_edgeOpPreviousShape;

    void beginInteractiveEdgeOp(EdgeOpType type);
    void updateInteractiveEdgeOp();
    void commitInteractiveEdgeOp();
    void cancelInteractiveEdgeOp();

    // Interactive extrude state
    bool m_extruding = false;
    TopoDS_Shape m_extrudeProfile;
    glm::vec3 m_extrudeNormal{0, 0, 1};
    glm::vec3 m_extrudeOrigin{0};
    float m_extrudeDistance = 5.0f;
    int m_extrudePreviewBodyId = -1;
    char m_extrudeInputBuf[32] = "5.0";
    bool m_extrudeInputFocus = true;

    // Right-click face context menu state
    int m_contextMenuBodyId = -1;
    TopoDS_Shape m_contextMenuFace;
    bool m_contextMenuPending = false;

    // Project file + dirty tracking
    std::string m_currentProjectPath;          // empty until first save/load
    int m_savedAtHistoryStep = -1;             // history index when last saved/loaded
    bool m_unsavedNonHistoryChanges = false;   // for mutations outside History (imports, etc.)

    // Close-with-unsaved-changes prompt
    bool m_showSavePrompt = false;
    bool m_confirmedClose = false;
    bool m_closeAfterSave = false;             // set when user picked Save in the prompt
};

} // namespace materializr
