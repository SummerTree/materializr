#include "UiTheme.h"
#include "ui_scale.h"
#include "ShortcutsPanel.h"
#include <imgui.h>

namespace materializr {

ShortcutsPanel::ShortcutsPanel() = default;

void ShortcutsPanel::setVisible(bool vis) {
    m_visible = vis;
}

bool ShortcutsPanel::isVisible() const {
    return m_visible;
}

void ShortcutsPanel::render() {
    if (!m_visible) return;

    ImGui::SetNextWindowSize(uiSz(420, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Keyboard Shortcuts", &m_visible)) {
        ImGui::End();
        return;
    }

    ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp;

    // File operations
    ImGui::TextColored(materializr::accentText(), "File");
    ImGui::Separator();
    if (ImGui::BeginTable("FileShortcuts", 2, tableFlags)) {
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Ctrl+S");
        ImGui::TableNextColumn(); ImGui::Text("Save");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Ctrl+O");
        ImGui::TableNextColumn(); ImGui::Text("Open");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Ctrl+I");
        ImGui::TableNextColumn(); ImGui::Text("Import STEP");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Ctrl+E");
        ImGui::TableNextColumn(); ImGui::Text("Export STEP");

        ImGui::EndTable();
    }

    ImGui::Spacing();

    // Edit operations
    ImGui::TextColored(materializr::accentText(), "Edit");
    ImGui::Separator();
    if (ImGui::BeginTable("EditShortcuts", 2, tableFlags)) {
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Ctrl+Z");
        ImGui::TableNextColumn(); ImGui::Text("Undo");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Ctrl+Y");
        ImGui::TableNextColumn(); ImGui::Text("Redo");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Ctrl+C");
        ImGui::TableNextColumn(); ImGui::Text("Copy");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Ctrl+V");
        ImGui::TableNextColumn(); ImGui::Text("Paste");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Ctrl+D");
        ImGui::TableNextColumn(); ImGui::Text("Duplicate");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Delete");
        ImGui::TableNextColumn(); ImGui::Text("Delete selected");

        ImGui::EndTable();
    }

    ImGui::Spacing();

    // Tools
    ImGui::TextColored(materializr::accentText(), "Tools");
    ImGui::Separator();
    if (ImGui::BeginTable("ToolShortcuts", 2, tableFlags)) {
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("S");
        ImGui::TableNextColumn(); ImGui::Text("Start Sketch");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("L");
        ImGui::TableNextColumn(); ImGui::Text("Line");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("C");
        ImGui::TableNextColumn(); ImGui::Text("Circle");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("R");
        ImGui::TableNextColumn(); ImGui::Text("Rectangle");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Escape");
        ImGui::TableNextColumn(); ImGui::Text("Cancel / Exit sketch");

        ImGui::EndTable();
    }

    ImGui::Spacing();

    // Navigation
    ImGui::TextColored(materializr::accentText(), "Navigation");
    ImGui::Separator();
    if (ImGui::BeginTable("NavShortcuts", 2, tableFlags)) {
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Home");
        ImGui::TableNextColumn(); ImGui::Text("Reset camera");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Middle Mouse");
        ImGui::TableNextColumn(); ImGui::Text("Orbit");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Shift+Middle Mouse");
        ImGui::TableNextColumn(); ImGui::Text("Pan");

        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Scroll Wheel");
        ImGui::TableNextColumn(); ImGui::Text("Zoom");

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace materializr
