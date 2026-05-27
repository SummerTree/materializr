#include "ItemsPanel.h"
#include "../core/Document.h"
#include "../core/History.h"
#include "../core/SelectionManager.h"
#include "../modeling/DeleteOp.h"
#include <imgui.h>
#include <cstring>
#include <cstdio>
#include <memory>

namespace materializr {

ItemsPanel::ItemsPanel() = default;

void ItemsPanel::setDocument(Document* doc) {
    m_document = doc;
}

void ItemsPanel::setSelectionManager(SelectionManager* sel) {
    m_selection = sel;
}

void ItemsPanel::setHistory(History* hist) {
    m_history = hist;
}

bool ItemsPanel::render() {
    m_bodyDeleted = false;
    ImGui::Begin("Items");

    if (!m_document) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No document loaded.");
        ImGui::End();
        return false;
    }

    // Filter toggles at top
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Filter");
    ImGui::Separator();

    if (ImGui::Button(m_showBodies ? "[Bodies]" : " Bodies ")) {
        m_showBodies = !m_showBodies;
    }
    ImGui::SameLine();
    if (ImGui::Button(m_showSketches ? "[Sketches]" : " Sketches ")) {
        m_showSketches = !m_showSketches;
    }
    ImGui::SameLine();
    if (ImGui::Button(m_showPlanes ? "[Planes]" : " Planes ")) {
        m_showPlanes = !m_showPlanes;
    }

    ImGui::Separator();

    // Bodies section
    if (m_showBodies) {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Bodies");

        std::vector<int> bodyIds = m_document->getAllBodyIds();

        for (int id : bodyIds) {
            ImGui::PushID(id);

            // Visibility toggle (eye icon)
            bool visible = m_document->isBodyVisible(id);
            if (ImGui::Checkbox("##vis", &visible)) {
                m_document->setBodyVisible(id, visible);
            }
            ImGui::SameLine();

            // Check if this body is currently selected
            bool isSelected = false;
            if (m_selection) {
                const auto& sel = m_selection->getSelection();
                for (const auto& entry : sel) {
                    if (entry.type == SelectionType::Body && entry.bodyId == id) {
                        isSelected = true;
                        break;
                    }
                }
            }

            // Renaming mode
            if (m_renamingId == id) {
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_AutoSelectAll)) {
                    m_document->setBodyName(id, m_renameBuffer);
                    m_renamingId = -1;
                }
                // Cancel on escape or click elsewhere
                if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                    (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))) {
                    m_renamingId = -1;
                }
            } else {
                // Selectable body name
                std::string name = m_document->getBodyName(id);
                if (ImGui::Selectable(name.c_str(), isSelected)) {
                    // Select this body
                    if (m_selection) {
                        SelectionEntry entry;
                        entry.type = SelectionType::Body;
                        entry.bodyId = id;
                        m_selection->select(entry);
                    }
                }

                // Double-click to rename
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    m_renamingId = id;
                    std::string bodyName = m_document->getBodyName(id);
                    std::strncpy(m_renameBuffer, bodyName.c_str(), sizeof(m_renameBuffer) - 1);
                    m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
                }

                // Right-click context menu
                if (ImGui::BeginPopupContextItem("BodyContextMenu")) {
                    if (ImGui::MenuItem("Rename")) {
                        m_renamingId = id;
                        std::string bodyName = m_document->getBodyName(id);
                        std::strncpy(m_renameBuffer, bodyName.c_str(), sizeof(m_renameBuffer) - 1);
                        m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
                    }
                    if (ImGui::MenuItem("Delete")) {
                        // Route through history so the delete is undoable
                        if (m_history) {
                            auto op = std::make_unique<DeleteOp>();
                            op->setBodyId(id);
                            m_history->pushOperation(std::move(op), *m_document);
                        } else {
                            m_document->removeBody(id);
                        }
                        if (m_selection) m_selection->clear();
                        m_renamingId = -1;
                        m_bodyDeleted = true;
                        ImGui::EndPopup();
                        ImGui::PopID();
                        goto end_bodies;
                    }
                    if (ImGui::MenuItem("Isolate")) {
                        // Hide all other bodies, show only this one
                        for (int otherId : bodyIds) {
                            m_document->setBodyVisible(otherId, otherId == id);
                        }
                    }
                    if (ImGui::MenuItem("Hide Others")) {
                        for (int otherId : bodyIds) {
                            if (otherId != id) {
                                m_document->setBodyVisible(otherId, false);
                            }
                        }
                    }
                    ImGui::EndPopup();
                }
            }

            ImGui::PopID();
        }
        end_bodies:;
    }

    // Sketches section
    if (m_showSketches) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Sketches");

        std::vector<int> sketchIds = m_document->getAllSketchIds();
        if (sketchIds.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(none)");
        }
        for (int id : sketchIds) {
            ImGui::PushID(1000000 + id); // namespace away from body ids

            bool visible = m_document->isSketchVisible(id);
            if (ImGui::Checkbox("##svis", &visible)) {
                m_document->setSketchVisible(id, visible);
            }
            ImGui::SameLine();

            bool isSelected = false;
            if (m_selection) {
                const auto& sel = m_selection->getSelection();
                for (const auto& e : sel) {
                    if (e.type == SelectionType::Sketch && e.sketchId == id) {
                        isSelected = true; break;
                    }
                }
            }

            // Rename ids are namespaced (1000000 + id) so they don't collide with
            // body rename ids in the shared m_renamingId.
            const int renameKey = 1000000 + id;
            auto beginRename = [&]() {
                m_renamingId = renameKey;
                std::string n = m_document->getSketchName(id);
                std::strncpy(m_renameBuffer, n.c_str(), sizeof(m_renameBuffer) - 1);
                m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
            };

            bool deleted = false;
            if (m_renamingId == renameKey) {
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##srename", m_renameBuffer, sizeof(m_renameBuffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_AutoSelectAll)) {
                    m_document->setSketchName(id, m_renameBuffer);
                    m_renamingId = -1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                    (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))) {
                    m_renamingId = -1;
                }
            } else {
                std::string name = m_document->getSketchName(id);
                if (ImGui::Selectable(name.c_str(), isSelected)) {
                    if (m_selection) {
                        SelectionEntry entry;
                        entry.type = SelectionType::Sketch;
                        entry.sketchId = id;
                        m_selection->select(entry);
                    }
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    beginRename();
                }

                if (ImGui::BeginPopupContextItem("SketchContextMenu")) {
                    if (ImGui::MenuItem("Rename")) {
                        beginRename();
                    }
                    if (ImGui::MenuItem("Delete")) {
                        m_document->removeSketch(id);
                        if (m_selection) m_selection->clear();
                        m_renamingId = -1;
                        deleted = true;
                    }
                    ImGui::EndPopup();
                }
            }

            ImGui::PopID();
            if (deleted) break; // sketchIds is now stale
        }
    }

    // Planes section (placeholder/future-ready)
    if (m_showPlanes) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Planes");

        if (ImGui::TreeNode("Planes##tree")) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(default planes)");
            ImGui::TreePop();
        }
    }

    // Bottom: counts
    ImGui::Separator();
    char countText[128];
    std::snprintf(countText, sizeof(countText), "Bodies: %d", m_document->bodyCount());
    ImGui::Text("%s", countText);

    ImGui::End();
    return m_bodyDeleted;
}

} // namespace materializr
