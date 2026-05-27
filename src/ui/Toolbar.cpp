#include "Toolbar.h"
#include "../core/SelectionManager.h"
#include "../plugin/PluginRegistry.h"
#include "../plugin/PluginContext.h"
#include "../plugin/Contributions.h"
#include <imgui.h>
#include <cmath>

namespace materializr {

Toolbar::Toolbar() = default;

void Toolbar::setSelectionManager(const SelectionManager* sel) {
    m_selection = sel;
}

ToolAction Toolbar::render() {
    ToolAction action = ToolAction::None;

    ImGui::Begin("Tools");

    if (m_sketchMode) {
        action = renderSketchTools();
    } else if (!m_selection || !m_selection->hasSelection()) {
        action = renderNoSelectionTools();
    } else if (m_selection->hasSelectedSketchRegions()) {
        action = renderSketchRegionTools();
    } else if (m_selection->hasSelectedSketches()) {
        action = renderSketchSelectedTools();
    } else if (m_selection->hasSelectedFaces()) {
        action = renderFaceTools();
        if (action == ToolAction::None) {
            action = renderBodyTools();
        }
    } else if (m_selection->hasSelectedBodies()) {
        action = renderBodyTools();
    } else if (m_selection->hasSelectedEdges()) {
        action = renderEdgeTools();
    } else {
        action = renderNoSelectionTools();
    }

    ImGui::End();
    return action;
}

void Toolbar::setSketchMode(bool active) {
    m_sketchMode = active;
}

bool Toolbar::isSketchMode() const {
    return m_sketchMode;
}

// Render plugin-contributed buttons matching any of the given contexts.
// contextMask is a bitmask: bit N = SelectionContext(N).
void Toolbar::renderPluginButtons(int contextMask) {
    if (!m_pluginCtx) return;
    auto& contribs = PluginRegistry::instance().toolbarContributions();
    std::string lastSection;
    for (size_t i = 0; i < contribs.size(); ++i) {
        auto& c = contribs[i];
        if (!((1 << static_cast<int>(c.context)) & contextMask)) continue;
        if (c.section != lastSection) {
            if (!lastSection.empty()) ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", c.section.c_str());
            ImGui::Separator();
            lastSection = c.section;
        }
        ImGui::PushID(static_cast<int>(i + 10000));
        if (ImGui::Button(c.name.c_str(), ImVec2(-1, 30))) {
            if (c.toolFactory) {
                PluginRegistry::instance().activateTool(c.toolFactory(), *m_pluginCtx);
            } else if (c.action) {
                c.action(*m_pluginCtx);
            }
        }
        ImGui::PopID();
    }
}

void Toolbar::renderGeneralSection() {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "General");
    ImGui::Separator();

    if (ImGui::Button("Measure", ImVec2(-1, 30)))
        ; // ToolAction::Measure handled elsewhere
    if (ImGui::Button("Reset Camera", ImVec2(-1, 30)))
        ; // handled via shortcut / command palette
}

ToolAction Toolbar::renderSketchTools() {
    ToolAction action = ToolAction::None;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Sketch Tools");
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Grid:");
    const float steps[] = { 0.1f, 0.5f, 1.0f, 10.0f };
    const char* labels[] = { "0.1", "0.5", "1", "10" };
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        bool selected = std::abs(m_gridStep - steps[i]) < 1e-6f;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
        if (ImGui::Button(labels[i], ImVec2(34, 24))) m_gridStep = steps[i];
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::Separator();

    if (ImGui::Button("Line", ImVec2(-1, 30)))      action = ToolAction::Line;
    if (ImGui::Button("Circle", ImVec2(-1, 30)))    action = ToolAction::Circle;
    if (ImGui::Button("Rectangle", ImVec2(-1, 30))) action = ToolAction::Rectangle;
    if (ImGui::Button("Arc", ImVec2(-1, 30)))       action = ToolAction::Arc;
    if (ImGui::Button("Spline", ImVec2(-1, 30)))    action = ToolAction::Spline;
    if (ImGui::Button("Polygon", ImVec2(-1, 30)))   action = ToolAction::Polygon;
    if (ImGui::Button("Trim", ImVec2(-1, 30)))      action = ToolAction::Trim;

    if (!m_cameraOrtho) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.85f, 1.0f));
        if (ImGui::Button("Look at Sketch", ImVec2(-1, 30)))
            action = ToolAction::LookAtSketch;
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    if (ImGui::Button("Finish Sketch", ImVec2(-1, 30)))
        action = ToolAction::FinishSketch;

    // Plugin buttons for InSketchMode context
    renderPluginButtons(1 << static_cast<int>(SelectionContext::InSketchMode));

    return action;
}

ToolAction Toolbar::renderNoSelectionTools() {
    ToolAction action = ToolAction::None;

    // Start a sketch on a base plane — lets you model from scratch with no body.
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Create");
    ImGui::Separator();
    if (ImGui::Button("Sketch on XY", ImVec2(-1, 30))) action = ToolAction::StartSketchXY;
    if (ImGui::Button("Sketch on XZ", ImVec2(-1, 30))) action = ToolAction::StartSketchXZ;
    if (ImGui::Button("Sketch on YZ", ImVec2(-1, 30))) action = ToolAction::StartSketchYZ;

    // Plugin buttons: NoSelection + Always
    int mask = (1 << static_cast<int>(SelectionContext::NoSelection))
             | (1 << static_cast<int>(SelectionContext::Always));
    renderPluginButtons(mask);

    return action;
}

ToolAction Toolbar::renderBodyTools() {
    ToolAction action = ToolAction::None;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Transform");
    ImGui::Separator();

    if (ImGui::Button("Move", ImVec2(-1, 30)))
        action = ToolAction::Move;

    ImGui::Checkbox("Snap to grid", &m_snapToGrid);
    const float gridSteps[] = { 0.1f, 0.5f, 1.0f, 10.0f };
    const char* gridLabels[] = { "0.1", "0.5", "1", "10" };
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        bool selected = std::abs(m_gridStep - gridSteps[i]) < 1e-6f;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
        ImGui::PushID(i);
        if (ImGui::SmallButton(gridLabels[i])) m_gridStep = gridSteps[i];
        ImGui::PopID();
        if (selected) ImGui::PopStyleColor();
    }

    // Plugin buttons: HasBodies + MultipleBodies
    int mask = (1 << static_cast<int>(SelectionContext::HasBodies))
             | (1 << static_cast<int>(SelectionContext::MultipleBodies));
    renderPluginButtons(mask);

    return action;
}

ToolAction Toolbar::renderFaceTools() {
    ToolAction action = ToolAction::None;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Face Operations");
    ImGui::Separator();

    if (ImGui::Button("Sketch on Face", ImVec2(-1, 30)))
        action = ToolAction::SketchOnFace;

    // Plugin buttons for HasFaces context
    renderPluginButtons(1 << static_cast<int>(SelectionContext::HasFaces));

    return action;
}

ToolAction Toolbar::renderSketchSelectedTools() {
    ToolAction action = ToolAction::None;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Sketch");
    ImGui::Separator();
    ImGui::TextWrapped("Tip: hover a sketch region to highlight it, click to select, Ctrl+click to add to selection.");
    ImGui::Separator();

    if (ImGui::Button("Edit Sketch", ImVec2(-1, 30)))
        action = ToolAction::EditSketch;
    if (ImGui::Button("Extrude Sketch", ImVec2(-1, 30)))
        action = ToolAction::ExtrudeSketch;

    // Plugin buttons for HasSketches context
    renderPluginButtons(1 << static_cast<int>(SelectionContext::HasSketches));

    return action;
}

ToolAction Toolbar::renderSketchRegionTools() {
    ToolAction action = ToolAction::None;

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Region");
    ImGui::Separator();
    int n = m_selection ? m_selection->selectedSketchRegionCount() : 0;
    ImGui::Text("%d region%s selected", n, n == 1 ? "" : "s");
    ImGui::Spacing();

    // Plugin buttons for HasSketchRegions context (Push/Pull etc.)
    renderPluginButtons(1 << static_cast<int>(SelectionContext::HasSketchRegions));

    // Edit the sketch this region belongs to — re-enter sketch mode to revise it.
    if (ImGui::Button("Edit Sketch", ImVec2(-1, 30)))
        action = ToolAction::EditSketch;

    ImGui::Spacing();
    ImGui::TextWrapped("Drag positive distance to extrude, negative to cut into the body the sketch sits on.");

    return action;
}

ToolAction Toolbar::renderEdgeTools() {
    ToolAction action = ToolAction::None;

    // Plugin buttons for HasEdges context
    renderPluginButtons(1 << static_cast<int>(SelectionContext::HasEdges));

    return action;
}

} // namespace materializr
