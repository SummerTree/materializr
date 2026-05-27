#pragma once
#include <functional>
#include <string>

class SelectionManager;

namespace materializr {

class PluginContext;

enum class ToolAction {
    None,
    // Sketch tools (still dispatched via ToolAction — tightly coupled to viewport)
    StartSketch, StartSketchXY, StartSketchXZ, StartSketchYZ,
    SketchOnFace, Line, Circle, Rectangle, Arc, Spline, Polygon, Trim,
    FinishSketch, EditSketch, ExtrudeSketch, PushPull, LookAtSketch,
    // 3D tools that still need the old dispatch path
    Extrude, Fillet, Chamfer,
    // Gizmo
    Move,
    // General
    Measure, ResetCamera
};

class Toolbar {
public:
    Toolbar();

    void setSelectionManager(const SelectionManager* sel);
    void setPluginContext(PluginContext* ctx) { m_pluginCtx = ctx; }

    ToolAction render();

    void setSketchMode(bool active);
    bool isSketchMode() const;

    void setGridStep(float step) { m_gridStep = step; }
    float getGridStep() const { return m_gridStep; }

    void setCameraOrtho(bool ortho) { m_cameraOrtho = ortho; }

    void setSnapToGrid(bool snap) { m_snapToGrid = snap; }
    bool getSnapToGrid() const { return m_snapToGrid; }

private:
    const SelectionManager* m_selection = nullptr;
    PluginContext* m_pluginCtx = nullptr;
    bool m_sketchMode = false;
    float m_gridStep = 1.0f;
    bool m_cameraOrtho = true;
    bool m_snapToGrid = true;

    ToolAction renderSketchTools();
    ToolAction renderSketchSelectedTools();
    ToolAction renderSketchRegionTools();
    ToolAction renderNoSelectionTools();
    ToolAction renderBodyTools();
    ToolAction renderFaceTools();
    ToolAction renderEdgeTools();

    void renderPluginButtons(int contextMask);
    void renderGeneralSection();
};

} // namespace materializr
