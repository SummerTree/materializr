#include "UiTheme.h"
#include "ui_scale.h"
#include "SettingsPanel.h"
#include <imgui.h>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>

namespace materializr {

SettingsPanel::SettingsPanel() = default;

AppSettings& SettingsPanel::getSettings() {
    return m_settings;
}

const AppSettings& SettingsPanel::getSettings() const {
    return m_settings;
}

void SettingsPanel::setVisible(bool vis) {
    m_visible = vis;
}

bool SettingsPanel::isVisible() const {
    return m_visible;
}

bool SettingsPanel::load(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        // Trim whitespace
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());

        if (key == "unitSystem") m_settings.unitSystem = std::atoi(value.c_str());
        else if (key == "gridSize") m_settings.gridSize = static_cast<float>(std::atof(value.c_str()));
        else if (key == "snapToGrid") m_settings.snapToGrid = (value == "1" || value == "true");
        else if (key == "snapThreshold") m_settings.snapThreshold = static_cast<float>(std::atof(value.c_str()));
        else if (key == "defaultExtrudeDistance") m_settings.defaultExtrudeDistance = std::atof(value.c_str());
        else if (key == "defaultFilletRadius") m_settings.defaultFilletRadius = std::atof(value.c_str());
        else if (key == "defaultChamferDistance") m_settings.defaultChamferDistance = std::atof(value.c_str());
        else if (key == "autoSaveIntervalSec") m_settings.autoSaveIntervalSec = std::atoi(value.c_str());
        else if (key == "autoSaveEnabled") m_settings.autoSaveEnabled = (value == "1" || value == "true");
        else if (key == "orbitSensitivity") m_settings.orbitSensitivity = static_cast<float>(std::atof(value.c_str()));
        else if (key == "panSensitivity") m_settings.panSensitivity = static_cast<float>(std::atof(value.c_str()));
        else if (key == "zoomSensitivity") m_settings.zoomSensitivity = static_cast<float>(std::atof(value.c_str()));
        else if (key == "showGrid") m_settings.showGrid = (value == "1" || value == "true");
        else if (key == "showEdgeWireframe") m_settings.showEdgeWireframe = (value == "1" || value == "true");
        else if (key == "showConstructionPlanes") m_settings.showConstructionPlanes = (value == "1" || value == "true");
    }

    return true;
}

bool SettingsPanel::save(const std::string& filePath) const {
    std::ofstream file(filePath);
    if (!file.is_open()) return false;

    file << "# Materializr Settings\n";
    file << "unitSystem=" << m_settings.unitSystem << "\n";
    file << "gridSize=" << m_settings.gridSize << "\n";
    file << "snapToGrid=" << (m_settings.snapToGrid ? "1" : "0") << "\n";
    file << "snapThreshold=" << m_settings.snapThreshold << "\n";
    file << "defaultExtrudeDistance=" << m_settings.defaultExtrudeDistance << "\n";
    file << "defaultFilletRadius=" << m_settings.defaultFilletRadius << "\n";
    file << "defaultChamferDistance=" << m_settings.defaultChamferDistance << "\n";
    file << "autoSaveIntervalSec=" << m_settings.autoSaveIntervalSec << "\n";
    file << "autoSaveEnabled=" << (m_settings.autoSaveEnabled ? "1" : "0") << "\n";
    file << "orbitSensitivity=" << m_settings.orbitSensitivity << "\n";
    file << "panSensitivity=" << m_settings.panSensitivity << "\n";
    file << "zoomSensitivity=" << m_settings.zoomSensitivity << "\n";
    file << "showGrid=" << (m_settings.showGrid ? "1" : "0") << "\n";
    file << "showEdgeWireframe=" << (m_settings.showEdgeWireframe ? "1" : "0") << "\n";
    file << "showConstructionPlanes=" << (m_settings.showConstructionPlanes ? "1" : "0") << "\n";

    return true;
}

bool SettingsPanel::render() {
    if (!m_visible) return false;

    bool changed = false;

    ImGui::SetNextWindowSize(uiSz(450, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &m_visible)) {
        ImGui::End();
        return false;
    }

    if (ImGui::BeginTabBar("SettingsTabs")) {
        // General tab
        if (ImGui::BeginTabItem("General")) {
            ImGui::TextColored(materializr::accentText(), "Units");
            ImGui::Separator();
            const char* unitItems[] = { "Millimeters (mm)", "Centimeters (cm)", "Inches (in)" };
            if (ImGui::Combo("Unit System", &m_settings.unitSystem, unitItems, 3)) {
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextColored(materializr::accentText(), "Auto-Save");
            ImGui::Separator();
            if (ImGui::Checkbox("Enable Auto-Save", &m_settings.autoSaveEnabled)) {
                changed = true;
            }
            if (m_settings.autoSaveEnabled) {
                if (ImGui::SliderInt("Interval (seconds)", &m_settings.autoSaveIntervalSec, 30, 1800)) {
                    changed = true;
                }
            }

            ImGui::EndTabItem();
        }

        // Display tab
        if (ImGui::BeginTabItem("Display")) {
            ImGui::TextColored(materializr::accentText(), "Visibility");
            ImGui::Separator();
            if (ImGui::Checkbox("Show Grid", &m_settings.showGrid)) {
                changed = true;
            }
            if (ImGui::Checkbox("Show Edge Wireframe", &m_settings.showEdgeWireframe)) {
                changed = true;
            }
            if (ImGui::Checkbox("Show Construction Planes", &m_settings.showConstructionPlanes)) {
                changed = true;
            }

            ImGui::Spacing();
            ImGui::TextColored(materializr::accentText(), "Grid");
            ImGui::Separator();
            if (ImGui::InputFloat("Grid Size", &m_settings.gridSize, 0.1f, 1.0f, "%.2f")) {
                if (m_settings.gridSize < 0.01f) m_settings.gridSize = 0.01f;
                changed = true;
            }
            // Snap-on/off + step are owned by the corner widget next to the
            // ViewCube (single source of truth). Threshold stays here as the
            // one snap parameter that isn't quick-toggled — only meaningful
            // when snap is on, but always editable so it doesn't disappear.
            ImGui::TextDisabled("Snap on/off + step: see the widget next to the ViewCube.");
            if (ImGui::SliderFloat("Snap Threshold", &m_settings.snapThreshold, 0.01f, 2.0f, "%.2f")) {
                changed = true;
            }

            ImGui::EndTabItem();
        }

        // Mouse tab
        if (ImGui::BeginTabItem("Mouse")) {
            ImGui::TextColored(materializr::accentText(), "Sensitivity");
            ImGui::Separator();
            if (ImGui::SliderFloat("Orbit", &m_settings.orbitSensitivity, 0.1f, 3.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Pan", &m_settings.panSensitivity, 0.1f, 3.0f, "%.2f")) {
                changed = true;
            }
            if (ImGui::SliderFloat("Zoom", &m_settings.zoomSensitivity, 0.1f, 3.0f, "%.2f")) {
                changed = true;
            }

            ImGui::EndTabItem();
        }

        // Defaults tab
        if (ImGui::BeginTabItem("Defaults")) {
            ImGui::TextColored(materializr::accentText(), "Operation Defaults");
            ImGui::Separator();

            double extrudeDist = m_settings.defaultExtrudeDistance;
            if (ImGui::InputDouble("Extrude Distance", &extrudeDist, 0.5, 5.0, "%.2f")) {
                if (extrudeDist > 0.0) {
                    m_settings.defaultExtrudeDistance = extrudeDist;
                    changed = true;
                }
            }

            double filletRad = m_settings.defaultFilletRadius;
            if (ImGui::InputDouble("Fillet Radius", &filletRad, 0.1, 1.0, "%.2f")) {
                if (filletRad > 0.0) {
                    m_settings.defaultFilletRadius = filletRad;
                    changed = true;
                }
            }

            double chamferDist = m_settings.defaultChamferDistance;
            if (ImGui::InputDouble("Chamfer Distance", &chamferDist, 0.1, 1.0, "%.2f")) {
                if (chamferDist > 0.0) {
                    m_settings.defaultChamferDistance = chamferDist;
                    changed = true;
                }
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
    return changed;
}

} // namespace materializr
