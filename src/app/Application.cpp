#include "gl_common.h"

#include "app/Application.h"
#include "app/Window.h"
#include "viewport/Viewport.h"
#include "viewport/Grid.h"
#include "viewport/ShapeRenderer.h"
#include "viewport/SketchRenderer.h"
#include "viewport/ViewCube.h"
#include "viewport/Picker.h"
#include "viewport/Gizmo.h"
#include "viewport/SelectionHighlight.h"
#include "viewport/EdgeRenderer.h"
#include "viewport/PlaneRenderer.h"
#include "viewport/BackgroundRenderer.h"
#include "core/Document.h"
#include "core/History.h"
#include "core/SelectionManager.h"
#include "ui/Toolbar.h"
#include "ui/HistoryPanel.h"
#include "ui/ItemsPanel.h"
#include "ui/CommandPalette.h"
#include "ui/StatusBar.h"
#include "ui/ThemeManager.h"
#include "ui/PropertiesPanel.h"
#include "modeling/Sketch.h"
#include "modeling/SketchSolver.h"
#include "modeling/SketchTool.h"
#include "modeling/ExtrudeOp.h"
#include "modeling/PushPullOp.h"
#include "modeling/TransformOp.h"
#include "modeling/FilletOp.h"
#include "modeling/ChamferOp.h"
#include "modeling/DeleteOp.h"
#include "modeling/SketchEditOp.h"
#include "io/StepIO.h"
#include "io/StlExport.h"
#include "io/FileDialogs.h"
#include "io/ProjectIO.h"
#include "core/EventBus.h"
#include "plugin/PluginContext.h"
#include "plugin/PluginRegistry.h"

namespace materializr { namespace force_link { void linkAll(); } }

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <gp_Ax3.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Plane.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepBuilderAPI_Transform.hxx>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <TopoDS.hxx>
#include <stdexcept>
#include <cstdio>

namespace materializr {

Application::Application() {
    m_window = std::make_unique<Window>(1600, 900, "Materializr");
    m_viewport = std::make_unique<Viewport>();
    m_grid = std::make_unique<Grid>();
    m_shapeRenderer = std::make_unique<ShapeRenderer>();
    m_sketchRenderer = std::make_unique<SketchRenderer>();
    m_edgeRenderer = std::make_unique<EdgeRenderer>();
    m_planeRenderer = std::make_unique<PlaneRenderer>();
    m_backgroundRenderer = std::make_unique<BackgroundRenderer>();
    m_document = std::make_unique<Document>();
    m_history = std::make_unique<History>();
    m_selection = std::make_unique<SelectionManager>();
    m_eventBus = std::make_unique<EventBus>();
    m_pluginContext = std::make_unique<PluginContext>();

    m_toolbar = std::make_unique<Toolbar>();
    m_historyPanel = std::make_unique<HistoryPanel>();
    m_itemsPanel = std::make_unique<ItemsPanel>();
    m_commandPalette = std::make_unique<CommandPalette>();

    m_sketchTool = std::make_unique<SketchTool>();
    m_viewCube = std::make_unique<ViewCube>();
    m_picker = std::make_unique<Picker>();
    m_gizmo = std::make_unique<Gizmo>();
    m_selectionHighlight = std::make_unique<SelectionHighlight>();
    m_statusBar = std::make_unique<StatusBar>();
    m_themeManager = std::make_unique<ThemeManager>();
    m_propertiesPanel = std::make_unique<PropertiesPanel>();

    // Wire up references
    m_toolbar->setSelectionManager(m_selection.get());
    m_toolbar->setPluginContext(m_pluginContext.get());
    m_historyPanel->setHistory(m_history.get());
    m_historyPanel->setDocument(m_document.get());
    m_itemsPanel->setDocument(m_document.get());
    m_itemsPanel->setSelectionManager(m_selection.get());
    m_itemsPanel->setHistory(m_history.get());
    m_statusBar->setDocument(m_document.get());
    m_statusBar->setSelectionManager(m_selection.get());
    m_propertiesPanel->setHistory(m_history.get());
    m_propertiesPanel->setDocument(m_document.get());
    m_propertiesPanel->setSelectionManager(m_selection.get());

    initImGui();
    m_themeManager->apply();
    initRenderers();
    setupCommands();

    // Wire EventBus into core services
    m_document->setEventBus(m_eventBus.get());
    m_history->setEventBus(m_eventBus.get());
    m_selection->setEventBus(m_eventBus.get());

    // Plugin system
    m_pluginContext->_bind(m_document.get(), m_history.get(), m_selection.get(),
                          m_eventBus.get(), &m_viewport->getCamera(), &m_meshesDirty);
    materializr::force_link::linkAll();
    PluginRegistry::instance().initAll(*m_pluginContext);

    // Register plugin commands in the command palette
    for (auto& cmd : PluginRegistry::instance().commandContributions()) {
        auto* ctxPtr = m_pluginContext.get();
        m_commandPalette->addCommand(cmd.name, cmd.shortcut, [&cmd, ctxPtr]() {
            if (cmd.action) cmd.action(*ctxPtr);
        });
    }
}

Application::~Application() {
    PluginRegistry::instance().shutdownAll();
    m_backgroundRenderer.reset();
    m_planeRenderer.reset();
    m_edgeRenderer.reset();
    m_sketchRenderer.reset();
    m_shapeRenderer.reset();
    m_grid.reset();
    m_viewport.reset();
    shutdownImGui();
}

static const char* s_defaultLayout = R"([Window][WindowOverViewport_11111111]
Pos=0,19
Size=1600,881
Collapsed=0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Viewport]
Pos=177,19
Size=1122,881
Collapsed=0
DockId=0x00000001,0

[Window][Tools]
Pos=0,19
Size=175,881
Collapsed=0
DockId=0x00000003,0

[Window][Interactions]
Pos=1301,19
Size=299,175
Collapsed=0
DockId=0x00000007,0

[Window][Items]
Pos=1301,197
Size=299,339
Collapsed=0
DockId=0x00000008,0

[Window][History]
Pos=1301,538
Size=299,362
Collapsed=0
DockId=0x00000006,1

[Window][Properties]
Pos=1301,538
Size=299,362
Collapsed=0
DockId=0x00000006,0

[Docking][Data]
DockSpace       ID=0x08BD597D Window=0x1BBC0F80 Pos=0,19 Size=1600,881 Split=X
  DockNode      ID=0x00000003 Parent=0x08BD597D SizeRef=175,900 Selected=0x18A5FDB9
  DockNode      ID=0x00000004 Parent=0x08BD597D SizeRef=1423,900 Split=X
    DockNode    ID=0x00000001 Parent=0x00000004 SizeRef=1122,900 CentralNode=1 Selected=0xC450F867
    DockNode    ID=0x00000002 Parent=0x00000004 SizeRef=299,900 Split=Y Selected=0x933ECD57
      DockNode  ID=0x00000005 Parent=0x00000002 SizeRef=148,528 Split=Y Selected=0x933ECD57
        DockNode ID=0x00000007 Parent=0x00000005 SizeRef=148,175 HiddenTabBar=1
        DockNode ID=0x00000008 Parent=0x00000005 SizeRef=148,348 Selected=0x933ECD57
      DockNode  ID=0x00000006 Parent=0x00000002 SizeRef=148,370 Selected=0x8C72BEA8
)";

void Application::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Write the default layout when imgui.ini is missing, or migrate a layout
    // saved before the Interactions panel existed (detected by its window name)
    // so the panel actually appears docked above Items rather than not at all.
    {
        bool needDefault = true;
        if (std::FILE* f = std::fopen("imgui.ini", "r")) {
            std::string ini;
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) ini.append(buf, n);
            std::fclose(f);
            if (ini.find("[Window][Interactions]") != std::string::npos) needDefault = false;
        }
        if (needDefault) {
            if (std::FILE* f = std::fopen("imgui.ini", "w")) {
                std::fputs(s_defaultLayout, f);
                std::fclose(f);
            }
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.14f, 0.18f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(m_window->handle(), true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void Application::shutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Application::initRenderers() {
    if (!m_grid->initialize()) {
        std::fprintf(stderr, "Failed to initialize grid renderer\n");
        return;
    }
    if (!m_shapeRenderer->initialize()) {
        std::fprintf(stderr, "Failed to initialize shape renderer\n");
        return;
    }
    if (!m_sketchRenderer->initialize()) {
        std::fprintf(stderr, "Failed to initialize sketch renderer\n");
        return;
    }

    if (!m_backgroundRenderer->initialize()) {
        std::fprintf(stderr, "Failed to initialize background renderer\n");
    }
    if (!m_selectionHighlight->initialize()) {
        std::fprintf(stderr, "Failed to initialize selection highlight\n");
    }
    if (!m_gizmo->initialize()) {
        std::fprintf(stderr, "Failed to initialize gizmo\n");
    }
    if (!m_edgeRenderer->initialize()) {
        std::fprintf(stderr, "Failed to initialize edge renderer\n");
    }
    if (!m_planeRenderer->initialize()) {
        std::fprintf(stderr, "Failed to initialize plane renderer\n");
    }

    // Create a demo box so there's something to see
    TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 2.0, 3.0).Shape();
    m_document->addBody(box, "Demo Box");
    m_meshesDirty = true;

    m_renderersReady = true;
}

void Application::setupCommands() {
    // Commands are now registered by plugins via PluginRegistry.
    // Plugin commands are added to the command palette after initAll().
}

void Application::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Application::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Application::renderDockspace() {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
}

void Application::renderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Project...", "Ctrl+O")) loadProject();
            if (ImGui::MenuItem("Save Project", "Ctrl+S")) saveProjectQuick();
            if (ImGui::MenuItem("Save Project As...")) saveProject();
            ImGui::Separator();

            // Build Import submenu from IOFormat contributions
            auto& formats = PluginRegistry::instance().ioFormats();
            bool hasImporters = false;
            for (auto& fmt : formats) { if (fmt.canImport) { hasImporters = true; break; } }
            if (hasImporters && ImGui::BeginMenu("Import")) {
                for (size_t i = 0; i < formats.size(); ++i) {
                    auto& fmt = formats[i];
                    if (!fmt.canImport || !fmt.importFn) continue;
                    ImGui::PushID(static_cast<int>(i));
                    std::string label = fmt.name + "...";
                    if (ImGui::MenuItem(label.c_str())) {
                        fmt.importFn(*m_pluginContext, "");
                    }
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }

            // Build Export submenu from IOFormat contributions
            bool hasExporters = false;
            for (auto& fmt : formats) { if (fmt.canExport) { hasExporters = true; break; } }
            if (hasExporters && ImGui::BeginMenu("Export")) {
                for (size_t i = 0; i < formats.size(); ++i) {
                    auto& fmt = formats[i];
                    if (!fmt.canExport || !fmt.exportFn) continue;
                    ImGui::PushID(static_cast<int>(i) + 1000);
                    std::string label = fmt.name + "...";
                    if (ImGui::MenuItem(label.c_str())) {
                        fmt.exportFn(*m_pluginContext, "");
                    }
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Settings...")) {
                // Stage the current bindings so the dialog can Cancel cleanly.
                m_settingsOrbitButton = m_orbitButton;
                m_settingsPanButton = m_panButton;
                m_showSettings = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) glfwSetWindowShouldClose(m_window->handle(), true);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_history->canUndo())) {
                m_history->undo(*m_document);
                m_meshesDirty = true;
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_history->canRedo())) {
                m_history->redo(*m_document);
                m_meshesDirty = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Camera", "Home")) m_viewport->getCamera().reset();
            if (ImGui::MenuItem("Command Palette", "Ctrl+K")) m_commandPalette->toggle();
            ImGui::Separator();
            if (m_themeManager->renderSelector()) {
                m_themeManager->apply();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

static const char* mouseButtonName(int b) {
    switch (b) {
        case 0: return "Left";
        case 1: return "Right";
        case 2: return "Middle";
        default: return "?";
    }
}

void Application::renderSettings() {
    if (!m_showSettings) return;
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Settings", &m_showSettings)) {
        ImGui::SeparatorText("Mouse — Camera");
        ImGui::TextWrapped("Choose which mouse button orbits and which pans. "
                           "Zoom is always the scroll wheel.");
        ImGui::Spacing();

        const char* buttons[] = { "Left", "Middle", "Right" };
        // Map button index <-> combo index (Left=0, Middle=2, Right=1 in ImGui).
        auto toCombo = [](int b) { return b == 0 ? 0 : (b == 2 ? 1 : 2); };
        auto fromCombo = [](int c) { return c == 0 ? 0 : (c == 1 ? 2 : 1); };

        int orbitC = toCombo(m_settingsOrbitButton);
        if (ImGui::Combo("Orbit", &orbitC, buttons, 3)) m_settingsOrbitButton = fromCombo(orbitC);
        int panC = toCombo(m_settingsPanButton);
        if (ImGui::Combo("Pan", &panC, buttons, 3)) m_settingsPanButton = fromCombo(panC);

        if (m_settingsOrbitButton == 0 || m_settingsPanButton == 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                "Note: Left is also used to select; assigning it here may conflict.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Apply", ImVec2(90, 0))) {
            m_orbitButton = m_settingsOrbitButton;
            m_panButton = m_settingsPanButton;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(90, 0))) {
            m_showSettings = false;
        }
    }
    ImGui::End();
}

void Application::renderInteractionsPanel() {
    // A quick-reference of the viewport interactions, docked above Items. The
    // camera rows reflect the live mouse bindings chosen in File > Settings.
    ImGui::Begin("Interactions");
    // Action label in a fixed left column, keys to its right. Keeping the action
    // first (it's short) means a long key string extends rightward instead of
    // overlapping the label.
    auto row = [](const char* action, const char* keys) {
        ImGui::TextUnformatted(action);
        ImGui::SameLine(120.0f);
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", keys);
    };
    char orbitKeys[32], panKeys[32];
    std::snprintf(orbitKeys, sizeof(orbitKeys), "%s-drag", mouseButtonName(m_orbitButton));
    std::snprintf(panKeys, sizeof(panKeys), "%s-drag", mouseButtonName(m_panButton));
    ImGui::SeparatorText("Camera");
    row("Orbit", orbitKeys);
    row("Pan", panKeys);
    row("Zoom", "Scroll wheel");
    row("Reset view", "Home");
    ImGui::SeparatorText("Select");
    row("Select face", "Click");
    row("Select body", "Double-click");
    row("Add to selection", "Ctrl+Click");
    row("Delete selected", "Del");
    ImGui::SeparatorText("Transform (body)");
    row("Move / Rotate / Scale", "W / E / R");
    ImGui::SeparatorText("Sketch");
    row("Start", "Pick a base plane / face");
    row("Finish / Cancel", "Enter / Esc");
    row("Dimension", "Type value + Enter");
    ImGui::SeparatorText("General");
    row("Undo / Redo", "Ctrl+Z / Ctrl+Y");
    row("Command palette", "Ctrl+K");
    ImGui::End();
}

void Application::renderViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    int w = static_cast<int>(contentSize.x);
    int h = static_cast<int>(contentSize.y);

    if (w > 0 && h > 0) {
        m_viewport->resize(w, h);

        if (m_meshesDirty) {
            rebuildMeshes();
            m_meshesDirty = false;
        }

        m_viewport->bind();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        m_backgroundRenderer->render();
        glEnable(GL_DEPTH_TEST);

        Camera& cam = m_viewport->getCamera();
        glm::mat4 view = cam.getViewMatrix();
        glm::mat4 proj = cam.getProjectionMatrix();

        // Grid: on a from-scratch (no-face) sketch, lay the world grid on the
        // sketch plane so it shows face-on; otherwise use the XZ ground plane.
        // Face-based sketches get their own face grid (drawn below) instead.
        {
            Grid::Plane gp; // defaults to the XZ ground
            bool baseSketch = m_inSketchMode && m_activeSketch &&
                              m_activeSketch->getSourceFace().IsNull();
            if (baseSketch) {
                const gp_Ax3& ax = m_activeSketch->getPlane().Position();
                auto v3 = [](const gp_Dir& d){ return glm::vec3(d.X(), d.Y(), d.Z()); };
                gp.origin = glm::vec3(ax.Location().X(), ax.Location().Y(), ax.Location().Z());
                gp.u = v3(ax.XDirection());
                gp.v = v3(ax.YDirection());
                gp.normal = v3(ax.Direction());
            }
            // Fade radius sized to the view so the grid fills it without a hard edge.
            float fadeDist = cam.isOrthographic()
                ? cam.getOrthoSize() * 8.0f
                : glm::length(cam.getPosition() - cam.getTarget()) * 8.0f;
            m_grid->render(view, proj, cam.getTarget(), std::max(fadeDist, 10.0f),
                           gp, std::max(m_sketchGridStep, 0.01f));
        }
        m_planeRenderer->render(view, proj);
        m_shapeRenderer->render(view, proj, cam.getPosition());
        m_edgeRenderer->render(view, proj);

        // Render selection highlight (face/edge/body)
        m_selectionHighlight->render(*m_selection, *m_document, view, proj);

        // Update gizmo visibility and position based on selection
        if (m_selection->hasSelectedBodies() && !m_inSketchMode && !m_extruding && !m_edgeOpActive) {
            const auto& sel = m_selection->getSelection();
            int bodyId = sel[0].bodyId;
            try {
                const TopoDS_Shape& shape = m_document->getBody(bodyId);
                Bnd_Box bbox;
                BRepBndLib::Add(shape, bbox);
                double xmin, ymin, zmin, xmax, ymax, zmax;
                bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                glm::vec3 center((xmin+xmax)*0.5f, (ymin+ymax)*0.5f, (zmin+zmax)*0.5f);
                m_gizmo->setPosition(center);
                m_gizmo->setVisible(true);
            } catch (...) {
                m_gizmo->setVisible(false);
            }
        } else {
            m_gizmo->setVisible(false);
        }

        if (m_gizmo->isVisible()) {
            m_gizmo->render(view, proj);
        }

        // Render all stored sketches (visible only) plus the active sketch
        for (int sid : m_document->getAllSketchIds()) {
            if (!m_document->isSketchVisible(sid)) continue;
            if (m_inSketchMode && sid == m_activeSketchId) continue; // drawn below with tool
            auto sk = m_document->getSketch(sid);
            if (sk) {
                m_sketchRenderer->render(sk.get(), nullptr, view, proj, nullptr);
            }
        }
        if (m_inSketchMode && m_activeSketch) {
            // Keep the tool's snap step in sync with the user-chosen grid
            m_sketchTool->setGridStep(m_sketchGridStep);
            // Face-based sketches get a face-local measurement grid bounded to the
            // host face. From-scratch (no-face) sketches instead use the world grid
            // laid on the sketch plane (set up where m_grid is rendered above), so
            // we only draw the face grid here when there is a source face.
            if (!m_activeSketch->getSourceFace().IsNull()) {
                float extent = std::max(20.0f, m_sketchGridStep * 40.0f);
                m_sketchRenderer->renderFaceGrid(m_activeSketch.get(),
                                                 extent,
                                                 m_sketchGridStep,
                                                 view, proj);
            }
            m_sketchRenderer->render(m_activeSketch.get(), m_sketchTool.get(), view, proj,
                                     m_sketchSolver.get());
        }

        // Highlight hovered/selected sketch regions
        auto highlightRegion = [&](int sketchId, int regionIdx, const glm::vec3& color, float w) {
            if (sketchId < 0 || regionIdx < 0) return;
            std::shared_ptr<Sketch> sk;
            if (sketchId == m_activeSketchId && m_activeSketch) sk = m_activeSketch;
            else sk = m_document->getSketch(sketchId);
            if (!sk) return;
            m_sketchRenderer->renderRegionBoundary(sk.get(), regionIdx, color, w, view, proj);
        };
        // Selected regions in solid yellow
        for (const auto& e : m_selection->getSelection()) {
            if (e.type == SelectionType::SketchRegion) {
                highlightRegion(e.sketchId, e.subShapeIndex,
                                glm::vec3(1.0f, 0.85f, 0.1f), 4.0f);
            }
        }
        // Hovered region in cyan (drawn last so it's on top)
        highlightRegion(m_hoveredSketchId, m_hoveredRegionIndex,
                        glm::vec3(0.2f, 0.9f, 1.0f), 3.0f);

        m_viewport->unbind();

        ImGui::Image(
            static_cast<ImTextureID>(m_viewport->getTextureID()),
            contentSize,
            ImVec2(0, 1),
            ImVec2(1, 0)
        );

        // --- Live dimension overlay: a measuring annotation (offset line with
        // arrowheads + value) shown while drawing a sketch line/circle, extruding,
        // or moving a body. Drawn in screen space over the viewport image. ---
        {
            ImVec2 imgMin = ImGui::GetItemRectMin();
            ImVec2 imgSize = ImGui::GetItemRectSize();
            glm::mat4 vpMat = proj * view;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            auto toImg = [&](glm::vec3 w, ImVec2& out) -> bool {
                glm::vec4 c = vpMat * glm::vec4(w, 1.0f);
                if (c.w <= 1e-5f) return false; // behind camera
                out = ImVec2(imgMin.x + (c.x / c.w * 0.5f + 0.5f) * imgSize.x,
                             imgMin.y + (1.0f - (c.y / c.w * 0.5f + 0.5f)) * imgSize.y);
                return true;
            };
            auto drawDim = [&](glm::vec3 aW, glm::vec3 bW, const char* label) {
                ImVec2 sa, sb;
                if (!toImg(aW, sa) || !toImg(bW, sb)) return;
                ImVec2 dir(sb.x - sa.x, sb.y - sa.y);
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                if (len < 2.0f) return;
                dir.x /= len; dir.y /= len;
                ImVec2 perp(-dir.y, dir.x);
                const float off = 26.0f, ah = 7.0f;
                ImVec2 da(sa.x + perp.x * off, sa.y + perp.y * off);
                ImVec2 db(sb.x + perp.x * off, sb.y + perp.y * off);
                ImU32 col = IM_COL32(235, 235, 240, 255);
                ImU32 ext = IM_COL32(170, 170, 180, 150);
                dl->AddLine(sa, da, ext, 1.0f);                 // extension lines
                dl->AddLine(sb, db, ext, 1.0f);
                dl->AddLine(da, db, col, 1.5f);                 // dimension line
                auto arrow = [&](ImVec2 tip, ImVec2 along) {
                    ImVec2 base(tip.x - along.x * ah, tip.y - along.y * ah);
                    dl->AddTriangleFilled(tip,
                        ImVec2(base.x + perp.x * ah * 0.5f, base.y + perp.y * ah * 0.5f),
                        ImVec2(base.x - perp.x * ah * 0.5f, base.y - perp.y * ah * 0.5f), col);
                };
                arrow(da, ImVec2(-dir.x, -dir.y));
                arrow(db, dir);
                ImVec2 ts = ImGui::CalcTextSize(label);
                ImVec2 mid((da.x + db.x) * 0.5f + perp.x * 12.0f,
                           (da.y + db.y) * 0.5f + perp.y * 12.0f);
                ImVec2 tp(mid.x - ts.x * 0.5f, mid.y - ts.y * 0.5f);
                dl->AddRectFilled(ImVec2(tp.x - 3, tp.y - 2), ImVec2(tp.x + ts.x + 3, tp.y + ts.y + 2),
                                  IM_COL32(20, 20, 28, 205), 3.0f);
                dl->AddText(tp, col, label);
            };

            char dbuf[40];
            if (m_extruding) {
                std::snprintf(dbuf, sizeof(dbuf), "%.1f mm", std::abs(m_extrudeDistance));
                drawDim(m_extrudeOrigin,
                        m_extrudeOrigin + m_extrudeNormal * m_extrudeDistance, dbuf);
            } else if (m_gizmoDragging && glm::length(m_gizmoTotalDelta) > 1e-3f) {
                // Translate drag: original body centre -> current centre.
                try {
                    Bnd_Box ob, cb;
                    BRepBndLib::Add(m_gizmoDragOriginalShape, ob);
                    BRepBndLib::Add(m_document->getBody(m_gizmoDragBodyId), cb);
                    if (!ob.IsVoid() && !cb.IsVoid()) {
                        double ox1, oy1, oz1, ox2, oy2, oz2, cx1, cy1, cz1, cx2, cy2, cz2;
                        ob.Get(ox1, oy1, oz1, ox2, oy2, oz2);
                        cb.Get(cx1, cy1, cz1, cx2, cy2, cz2);
                        glm::vec3 oc((ox1 + ox2) * 0.5, (oy1 + oy2) * 0.5, (oz1 + oz2) * 0.5);
                        glm::vec3 cc((cx1 + cx2) * 0.5, (cy1 + cy2) * 0.5, (cz1 + cz2) * 0.5);
                        float dist = glm::length(cc - oc);
                        if (dist > 1e-3f) {
                            std::snprintf(dbuf, sizeof(dbuf), "%.1f mm", dist);
                            drawDim(oc, cc, dbuf);
                        }
                    }
                } catch (...) {}
            }

            // Sketch preview dimensions: line length, circle diameter (across the
            // full width — makers dimension by diameter), rectangle both sides.
            if (m_inSketchMode && m_activeSketch && m_sketchTool && m_sketchTool->hasPreview()) {
                const gp_Ax3& ax = m_activeSketch->getPlane().Position();
                glm::vec3 O(ax.Location().X(), ax.Location().Y(), ax.Location().Z());
                glm::vec3 X(ax.XDirection().X(), ax.XDirection().Y(), ax.XDirection().Z());
                glm::vec3 Y(ax.YDirection().X(), ax.YDirection().Y(), ax.YDirection().Z());
                auto sketch2world = [&](glm::vec2 p) { return O + p.x * X + p.y * Y; };

                SketchToolMode pm = m_sketchTool->getPreviewType();
                glm::vec2 ps = m_sketchTool->getPreviewStart();
                glm::vec2 pe = m_sketchTool->getPreviewEnd();

                if (pm == SketchToolMode::Line) {
                    float length = glm::length(pe - ps);
                    if (length > 1e-3f) {
                        std::snprintf(dbuf, sizeof(dbuf), "%.1f mm", length);
                        drawDim(sketch2world(ps), sketch2world(pe), dbuf);
                    }
                } else if (pm == SketchToolMode::Circle) {
                    // ps = centre, pe = a point on the rim. Span the full diameter.
                    glm::vec2 rvec = pe - ps;
                    float dia = 2.0f * glm::length(rvec);
                    if (dia > 1e-3f) {
                        std::snprintf(dbuf, sizeof(dbuf), "%.1f mm dia", dia);
                        drawDim(sketch2world(ps - rvec), sketch2world(pe), dbuf);
                    }
                } else if (pm == SketchToolMode::Rectangle) {
                    // ps, pe = opposite corners. Dimension the bottom and right sides.
                    glm::vec2 bl(ps.x, ps.y), br(pe.x, ps.y), tr(pe.x, pe.y);
                    float w = std::abs(pe.x - ps.x), h = std::abs(pe.y - ps.y);
                    if (w > 1e-3f) {
                        std::snprintf(dbuf, sizeof(dbuf), "%.1f mm", w);
                        drawDim(sketch2world(bl), sketch2world(br), dbuf);
                    }
                    if (h > 1e-3f) {
                        std::snprintf(dbuf, sizeof(dbuf), "%.1f mm", h);
                        drawDim(sketch2world(br), sketch2world(tr), dbuf);
                    }
                }
            }
        }

        if (ImGui::IsItemHovered()) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.MouseWheel != 0.0f) cam.zoom(io.MouseWheel);
            // Camera drag uses the configurable bindings (File > Settings). The
            // orbit button pans instead when Shift is held; a distinct pan button
            // always pans.
            if (ImGui::IsMouseDragging(m_orbitButton)) {
                ImVec2 delta = io.MouseDelta;
                if (io.KeyShift) cam.pan(delta.x, delta.y);
                else cam.orbit(delta.x, delta.y);
            }
            if (m_panButton != m_orbitButton && ImGui::IsMouseDragging(m_panButton)) {
                cam.pan(io.MouseDelta.x, io.MouseDelta.y);
            }

            // Interactive extrude drag: left-drag moves distance along normal
            if (m_extruding && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                // Project mouse delta onto the screen-space direction of the extrude normal
                glm::mat4 vp = proj * view;
                glm::vec3 originScreen = glm::vec3(vp * glm::vec4(m_extrudeOrigin, 1.0f));
                glm::vec3 normalTip = m_extrudeOrigin + m_extrudeNormal;
                glm::vec3 tipScreen = glm::vec3(vp * glm::vec4(normalTip, 1.0f));
                glm::vec2 screenDir = glm::normalize(glm::vec2(tipScreen.x - originScreen.x,
                                                                 -(tipScreen.y - originScreen.y)));
                glm::vec2 mouseDelta(io.MouseDelta.x, io.MouseDelta.y);
                float proj_amount = glm::dot(mouseDelta, screenDir) * 0.05f;
                m_extrudeDistance += proj_amount;
                std::snprintf(m_extrudeInputBuf, sizeof(m_extrudeInputBuf), "%.1f", m_extrudeDistance);
                updateInteractiveExtrude();
            }

            // Gizmo input + Face hover highlighting + picking (when not in sketch mode)
            if (!m_inSketchMode && !m_extruding) {
                ImVec2 mousePos = ImGui::GetMousePos();
                ImVec2 winPos = ImGui::GetItemRectMin();
                float localX = mousePos.x - winPos.x;
                float localY = mousePos.y - winPos.y;

                // Gizmo interaction takes priority
                bool gizmoConsumedInput = false;
                if (m_gizmo->isVisible()) {
                    bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
                    bool mouseJustPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

                    GizmoResult gResult = m_gizmo->handleInput(
                        localX, localY, contentSize.x, contentSize.y,
                        mouseDown, mouseJustPressed, cam);

                    // Start drag: save original shape and reset accumulators
                    if (gResult.activeAxis != GizmoAxis::None && !m_gizmoDragging) {
                        int bodyId = m_selection->getSelection()[0].bodyId;
                        try {
                            m_gizmoDragOriginalShape = m_document->getBody(bodyId);
                            m_gizmoDragBodyId = bodyId;
                            m_gizmoDragging = true;
                            m_gizmoTotalDelta = glm::vec3(0.0f);
                        } catch (...) {}
                    }

                    // During drag: apply live preview
                    if (gResult.changed && m_gizmoDragging) {
                        try {
                            gp_Trsf trsf;
                            // Whether to apply transform to the ORIGINAL shape or the
                            // current (already-transformed) shape. Translate snaps the
                            // total drag delta in absolute world space, so it has to
                            // re-transform the original each frame. Rotate/Scale stay
                            // on the existing per-frame approach.
                            bool applyToOriginal = false;

                            if (gResult.mode == GizmoMode::Translate) {
                                // Accumulate the per-frame deltas so we always know
                                // the total movement since the drag started, then snap
                                // THAT (not per-frame) to grid multiples and apply to
                                // the original shape. This gives clean integer-multiple
                                // displacements regardless of where the body started.
                                m_gizmoTotalDelta += gResult.delta;
                                glm::vec3 d = m_gizmoTotalDelta;
                                if (m_snapToGrid && m_sketchGridStep > 0.0f) {
                                    float step = m_sketchGridStep;
                                    float thr = step * 0.4f;
                                    auto snap1d = [&](float v) {
                                        float nearest = std::round(v / step) * step;
                                        return (std::abs(v - nearest) < thr) ? nearest : v;
                                    };
                                    d.x = snap1d(d.x);
                                    d.y = snap1d(d.y);
                                    d.z = snap1d(d.z);
                                }
                                trsf.SetTranslation(gp_Vec(d.x, d.y, d.z));
                                applyToOriginal = true;
                            } else if (gResult.mode == GizmoMode::Rotate) {
                                double angle = glm::length(gResult.delta);
                                if (angle > 1e-6) {
                                    glm::vec3 axis = glm::normalize(gResult.delta);
                                    const TopoDS_Shape& cur = m_document->getBody(m_gizmoDragBodyId);
                                    Bnd_Box bbox;
                                    BRepBndLib::Add(cur, bbox);
                                    double xmin,ymin,zmin,xmax,ymax,zmax;
                                    bbox.Get(xmin,ymin,zmin,xmax,ymax,zmax);
                                    gp_Pnt center((xmin+xmax)/2,(ymin+ymax)/2,(zmin+zmax)/2);
                                    trsf.SetRotation(gp_Ax1(center, gp_Dir(axis.x, axis.y, axis.z)),
                                                     angle * M_PI / 180.0);
                                }
                            } else if (gResult.mode == GizmoMode::Scale) {
                                float axisDelta = 0.0f;
                                if (gResult.activeAxis == GizmoAxis::X) axisDelta = gResult.delta.x;
                                else if (gResult.activeAxis == GizmoAxis::Y) axisDelta = gResult.delta.y;
                                else if (gResult.activeAxis == GizmoAxis::Z) axisDelta = gResult.delta.z;

                                const TopoDS_Shape& cur = m_document->getBody(m_gizmoDragBodyId);
                                Bnd_Box bbox;
                                BRepBndLib::Add(cur, bbox);
                                double xmin,ymin,zmin,xmax,ymax,zmax;
                                bbox.Get(xmin,ymin,zmin,xmax,ymax,zmax);
                                float bodySize = static_cast<float>(glm::length(
                                    glm::vec3(xmax-xmin, ymax-ymin, zmax-zmin)));
                                if (bodySize < 0.001f) bodySize = 1.0f;

                                double factor = 1.0 + static_cast<double>(axisDelta) / bodySize;
                                factor = glm::clamp(factor, 0.1, 10.0);
                                gp_Pnt center((xmin+xmax)/2,(ymin+ymax)/2,(zmin+zmax)/2);
                                trsf.SetScale(center, factor);
                            }

                            const TopoDS_Shape& base = applyToOriginal
                                ? m_gizmoDragOriginalShape
                                : m_document->getBody(m_gizmoDragBodyId);
                            BRepBuilderAPI_Transform xform(base, trsf, true);
                            if (xform.IsDone()) {
                                m_document->updateBody(m_gizmoDragBodyId, xform.Shape());
                                m_meshesDirty = true;
                            }
                        } catch (...) {}
                        gizmoConsumedInput = true;
                    }

                    // End drag: commit to history as a TransformOp
                    if (m_gizmoDragging && gResult.activeAxis == GizmoAxis::None && !mouseDown) {
                        try {
                            // Capture the final transformed shape
                            TopoDS_Shape finalShape = m_document->getBody(m_gizmoDragBodyId);
                            // Restore original so the op's execute() saves it for undo
                            m_document->updateBody(m_gizmoDragBodyId, m_gizmoDragOriginalShape);

                            // Compute centroid delta for the TransformOp
                            Bnd_Box origBox, finalBox;
                            BRepBndLib::Add(m_gizmoDragOriginalShape, origBox);
                            BRepBndLib::Add(finalShape, finalBox);
                            double ox1,oy1,oz1,ox2,oy2,oz2, fx1,fy1,fz1,fx2,fy2,fz2;
                            origBox.Get(ox1,oy1,oz1,ox2,oy2,oz2);
                            finalBox.Get(fx1,fy1,fz1,fx2,fy2,fz2);
                            double dx = ((fx1+fx2)-(ox1+ox2))/2;
                            double dy = ((fy1+fy2)-(oy1+oy2))/2;
                            double dz = ((fz1+fz2)-(oz1+oz2))/2;

                            auto op = std::make_unique<TransformOp>();
                            op->setBodyId(m_gizmoDragBodyId);
                            op->setType(TransformType::Translate);
                            op->setTranslation(dx, dy, dz);
                            m_history->pushOperation(std::move(op), *m_document);
                            m_meshesDirty = true;
                        } catch (...) {}

                        m_gizmoDragging = false;
                        m_gizmoDragOriginalShape.Nullify();
                        m_gizmoDragBodyId = -1;
                    }

                    if (gResult.activeAxis != GizmoAxis::None) {
                        gizmoConsumedInput = true;
                    }
                }

                if (!gizmoConsumedInput) {
                    auto result = m_picker->pick(localX, localY,
                        contentSize.x, contentSize.y, cam, *m_document);

                    m_hoveredBodyId = result.hit ? result.bodyId : -1;

                    // Sketch-region hover (takes priority over body picking when present)
                    SketchRegionHit regionHit = pickSketchRegion(localX, localY,
                        contentSize.x, contentSize.y);
                    // Reject a sketch region that sits behind a body under the cursor —
                    // only what's visible should be selectable. Compare hit distances
                    // from the camera (origin-independent) and drop the region if the
                    // body face is nearer.
                    if (regionHit.regionIndex >= 0 && result.hit) {
                        glm::vec3 camPos = cam.getPosition();
                        float bodyD = glm::length(result.hitPoint - camPos);
                        float sketchD = glm::length(regionHit.worldPoint - camPos);
                        if (bodyD < sketchD - 1e-3f) {
                            regionHit.sketchId = -1;
                            regionHit.regionIndex = -1;
                        }
                    }
                    m_hoveredSketchId = regionHit.sketchId;
                    m_hoveredRegionIndex = regionHit.regionIndex;

                    bool regionConsumedClick = false;
                    if (regionHit.regionIndex >= 0 &&
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        SelectionEntry entry;
                        entry.type = SelectionType::SketchRegion;
                        entry.sketchId = regionHit.sketchId;
                        entry.subShapeIndex = regionHit.regionIndex;
                        if (io.KeyCtrl) {
                            m_selection->addToSelection(entry);
                        } else {
                            m_selection->select(entry);
                        }
                        regionConsumedClick = true;
                    }

                    // Double-click to select body, single-click to select face
                    if (!regionConsumedClick && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (result.hit) {
                            SelectionEntry entry;
                            entry.type = SelectionType::Body;
                            entry.bodyId = result.bodyId;
                            try { entry.shape = m_document->getBody(result.bodyId); } catch (...) {}
                            if (io.KeyCtrl) {
                                m_selection->addToSelection(entry);
                            } else {
                                m_selection->select(entry);
                            }
                        }
                    } else if (!regionConsumedClick && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        if (result.hit) {
                            SelectionEntry entry;
                            // If click is near an edge (<8px), select edge; otherwise face
                            if (result.edgeScreenDist < 8.0f && !result.nearestEdge.IsNull()) {
                                entry.type = SelectionType::Edge;
                                entry.bodyId = result.bodyId;
                                entry.shape = result.nearestEdge;
                            } else {
                                entry.type = SelectionType::Face;
                                entry.bodyId = result.bodyId;
                                entry.subShapeIndex = result.faceIndex;
                                entry.shape = result.pickedShape;
                            }
                            if (io.KeyCtrl) {
                                m_selection->addToSelection(entry);
                            } else {
                                m_selection->select(entry);
                            }
                        } else {
                            m_selection->clear();
                        }
                    }

                    // Right click on a face: context menu (only if not a pan drag)
                    ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                    bool wasDragging = (std::abs(dragDelta.x) > 1.0f || std::abs(dragDelta.y) > 1.0f);
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !wasDragging) {
                        if (result.hit && !result.pickedShape.IsNull()) {
                            m_contextMenuBodyId = result.bodyId;
                            m_contextMenuFace = result.pickedShape;
                            m_contextMenuPending = true;
                        }
                    }
                }
            }

            // Sketch mode mouse input — ray-plane intersection
            if (m_inSketchMode && m_activeSketch) {
                ImVec2 mousePos = ImGui::GetMousePos();
                ImVec2 winPos = ImGui::GetItemRectMin();
                float localX = mousePos.x - winPos.x;
                float localY = mousePos.y - winPos.y;
                glm::vec2 sketchCoord = screenToSketch(localX, localY, contentSize.x, contentSize.y);

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    recordSketchMutation([&]{ m_sketchTool->onMouseDown(sketchCoord); });
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    m_sketchTool->onMouseUp(sketchCoord);
                }
                m_sketchTool->onMouseMove(sketchCoord);
            }
        }
    }

    // ViewCube overlay
    ViewCubeAction vcAction = m_viewCube->render(m_viewport->getCamera());
    if (vcAction != ViewCubeAction::None) {
        handleViewCubeAction(static_cast<int>(vcAction));
    }

    // Right-click face context menu
    if (m_contextMenuPending) {
        ImGui::OpenPopup("FaceContextMenu");
        m_contextMenuPending = false;
    }
    if (ImGui::BeginPopup("FaceContextMenu")) {
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Face Options");
        ImGui::Separator();

        if (ImGui::MenuItem("Sketch on this Face")) {
            // Select the face, then enter sketch mode (enterSketchMode reads the selection)
            SelectionEntry entry;
            entry.type = SelectionType::Face;
            entry.bodyId = m_contextMenuBodyId;
            entry.shape = m_contextMenuFace;
            m_selection->select(entry);
            enterSketchMode();
            m_contextMenuFace.Nullify();
        }
        if (ImGui::MenuItem("Extrude Face")) {
            beginInteractiveExtrude(m_contextMenuFace);
            m_contextMenuFace.Nullify();
        }
        if (ImGui::MenuItem("Select Body")) {
            SelectionEntry entry;
            entry.type = SelectionType::Body;
            entry.bodyId = m_contextMenuBodyId;
            try { entry.shape = m_document->getBody(m_contextMenuBodyId); } catch (...) {}
            m_selection->select(entry);
            m_contextMenuFace.Nullify();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Cancel")) {
            m_contextMenuFace.Nullify();
        }
        ImGui::EndPopup();
    }

    // Gizmo hint
    if (m_gizmo->isVisible()) {
        ImGui::SetCursorPos(ImVec2(10, 30));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::Text("Arrows: Move | Rings: Rotate | Cubes: Scale");
        ImGui::PopStyleColor();
    }

    // Interactive extrude UI
    if (m_extruding) {
        ImGui::SetCursorPos(ImVec2(10, 30));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        ImGui::Text("EXTRUDE - Drag in viewport or type distance. Enter to confirm, Escape to cancel.");
        ImGui::PopStyleColor();

        // Floating distance input panel
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 260,
                                        ImGui::GetWindowPos().y + 50));
        ImGui::SetNextWindowSize(ImVec2(240, 0));
        ImGui::Begin("##ExtrudeInput", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("Extrude Distance (mm)");
        ImGui::Separator();

        if (m_extrudeInputFocus) {
            ImGui::SetKeyboardFocusHere();
            m_extrudeInputFocus = false;
        }

        bool valueChanged = false;
        if (ImGui::InputText("##dist", m_extrudeInputBuf, sizeof(m_extrudeInputBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            // Enter pressed — commit
            m_extrudeDistance = static_cast<float>(std::atof(m_extrudeInputBuf));
            updateInteractiveExtrude();
            commitInteractiveExtrude();
        } else {
            // Update distance from text as user types
            float parsed = static_cast<float>(std::atof(m_extrudeInputBuf));
            if (std::abs(parsed - m_extrudeDistance) > 0.01f && std::abs(parsed) > 0.01f) {
                m_extrudeDistance = parsed;
                updateInteractiveExtrude();
            }
        }

        ImGui::SameLine();
        ImGui::Text("mm");

        // Slider for quick adjustment
        if (ImGui::SliderFloat("##slider", &m_extrudeDistance, -50.0f, 50.0f, "%.1f mm")) {
            std::snprintf(m_extrudeInputBuf, sizeof(m_extrudeInputBuf), "%.1f", m_extrudeDistance);
            updateInteractiveExtrude();
        }

        ImGui::Spacing();
        if (ImGui::Button("Confirm (Enter)", ImVec2(110, 0))) {
            commitInteractiveExtrude();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel (Esc)", ImVec2(110, 0))) {
            cancelInteractiveExtrude();
        }

        ImGui::End();
    }

    // Interactive Push/Pull UI
    if (m_pushPullActive) {
        ImGui::SetCursorPos(ImVec2(10, 30));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.85f, 1.0f, 1.0f));
        ImGui::Text("PUSH/PULL - Positive = extrude, Negative = cut. Enter to confirm, Escape to cancel.");
        ImGui::PopStyleColor();

        ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 260,
                                        ImGui::GetWindowPos().y + 50));
        ImGui::SetNextWindowSize(ImVec2(240, 0));
        ImGui::Begin("##PushPullInput", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("Distance (mm) - signed");
        ImGui::Separator();

        if (m_pushPullInputFocus) {
            ImGui::SetKeyboardFocusHere();
            m_pushPullInputFocus = false;
        }

        if (ImGui::InputText("##ppdist", m_pushPullInputBuf, sizeof(m_pushPullInputBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_pushPullDistance = static_cast<float>(std::atof(m_pushPullInputBuf));
            updatePushPull();
            commitPushPull();
        } else {
            float parsed = static_cast<float>(std::atof(m_pushPullInputBuf));
            if (std::abs(parsed - m_pushPullDistance) > 0.01f) {
                m_pushPullDistance = parsed;
                updatePushPull();
            }
        }

        ImGui::SameLine();
        ImGui::Text("mm");

        if (ImGui::SliderFloat("##ppslider", &m_pushPullDistance, -50.0f, 50.0f, "%.1f mm")) {
            std::snprintf(m_pushPullInputBuf, sizeof(m_pushPullInputBuf), "%.1f", m_pushPullDistance);
            updatePushPull();
        }

        ImGui::Spacing();
        if (ImGui::Button("Confirm (Enter)", ImVec2(110, 0))) {
            commitPushPull();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel (Esc)", ImVec2(110, 0))) {
            cancelPushPull();
        }

        ImGui::End();
    }

    // Interactive fillet/chamfer UI
    if (m_edgeOpActive) {
        const char* opName = m_edgeOpType == EdgeOpType::Fillet ? "FILLET" : "CHAMFER";
        const char* label = m_edgeOpType == EdgeOpType::Fillet ? "Radius (mm)" : "Distance (mm)";

        ImGui::SetCursorPos(ImVec2(10, 30));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.5f, 1.0f));
        ImGui::Text("%s - Type value or use slider. Enter to confirm, Escape to cancel.", opName);
        ImGui::PopStyleColor();

        ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 260,
                                        ImGui::GetWindowPos().y + 50));
        ImGui::SetNextWindowSize(ImVec2(240, 0));
        ImGui::Begin("##EdgeOpInput", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("%s", label);
        ImGui::Separator();

        if (m_edgeOpInputFocus) {
            ImGui::SetKeyboardFocusHere();
            m_edgeOpInputFocus = false;
        }

        if (ImGui::InputText("##val", m_edgeOpInputBuf, sizeof(m_edgeOpInputBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_edgeOpValue = static_cast<float>(std::atof(m_edgeOpInputBuf));
            updateInteractiveEdgeOp();
            commitInteractiveEdgeOp();
        } else {
            float parsed = static_cast<float>(std::atof(m_edgeOpInputBuf));
            if (std::abs(parsed - m_edgeOpValue) > 0.01f && parsed > 0.01f) {
                m_edgeOpValue = parsed;
                updateInteractiveEdgeOp();
            }
        }

        ImGui::SameLine();
        ImGui::Text("mm");

        if (ImGui::SliderFloat("##eslider", &m_edgeOpValue, 0.1f, 20.0f, "%.1f mm")) {
            std::snprintf(m_edgeOpInputBuf, sizeof(m_edgeOpInputBuf), "%.1f", m_edgeOpValue);
            updateInteractiveEdgeOp();
        }

        ImGui::Spacing();
        if (ImGui::Button("Confirm (Enter)", ImVec2(110, 0))) {
            commitInteractiveEdgeOp();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel (Esc)", ImVec2(110, 0))) {
            cancelInteractiveEdgeOp();
        }

        ImGui::End();
    }

    // Sketch mode indicator
    if (m_inSketchMode) {
        ImGui::SetCursorPos(ImVec2(10, 30));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));
        ImGui::Text("SKETCH MODE - Press Escape to finish");
        ImGui::PopStyleColor();
    }

    // Inline dimension input while placing a sketch shape
    if (m_inSketchMode && m_sketchTool && m_sketchTool->hasPreview()) {
        SketchToolMode mode = m_sketchTool->getPreviewType();
        const char* dimLabel = nullptr;
        switch (mode) {
            case SketchToolMode::Line:      dimLabel = "Length (mm)"; break;
            case SketchToolMode::Circle:    dimLabel = "Radius (mm)"; break;
            case SketchToolMode::Polygon:   dimLabel = "Radius (mm)"; break;
            case SketchToolMode::Rectangle: dimLabel = "Side (mm)";   break;
            default: dimLabel = nullptr;
        }
        if (dimLabel) {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 230,
                                            ImGui::GetWindowPos().y + 50));
            ImGui::SetNextWindowSize(ImVec2(220, 0));
            ImGui::Begin("##SketchDimInput", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_AlwaysAutoResize);

            ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "%s", dimLabel);
            ImGui::Separator();
            ImGui::TextWrapped("Type a value and press Enter. The shape extends from your first click toward the cursor.");

            // Grab keyboard focus the first frame placement begins
            if (!m_sketchDimWasShown) {
                ImGui::SetKeyboardFocusHere();
                m_sketchDimWasShown = true;
            }

            if (ImGui::InputText("##sketchDim", m_sketchDimBuf, sizeof(m_sketchDimBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue |
                                 ImGuiInputTextFlags_CharsDecimal |
                                 ImGuiInputTextFlags_AutoSelectAll)) {
                float v = static_cast<float>(std::atof(m_sketchDimBuf));
                if (v > 0.0f) {
                    recordSketchMutation([&]{ m_sketchTool->applyDimension(v); });
                }
                m_sketchDimBuf[0] = '\0';
                m_sketchDimWasShown = false; // re-focus on the next placement
            }

            ImGui::End();
        }
    } else {
        // Reset when not placing
        m_sketchDimBuf[0] = '\0';
        m_sketchDimWasShown = false;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void Application::handleToolAction(int action) {
    ToolAction a = static_cast<ToolAction>(action);
    switch (a) {
        case ToolAction::StartSketch: enterSketchMode(); break;
        // Each plane is built so that alignCameraToActiveSketch (camera at +normal,
        // up = plane YDirection) reproduces the matching ViewCube view exactly, in
        // this Y-up world: XY = Top, XZ = Front, YZ = Right. gp_Ax3(origin, normal,
        // xDir); YDirection (the camera up) = normal × xDir.
        case ToolAction::StartSketchXY: // Top: camera +Y, up -Z
            enterSketchOnPlane(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0), gp_Dir(1, 0, 0)))); break;
        case ToolAction::StartSketchXZ: // Front: camera +Z, up +Y
            enterSketchOnPlane(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)))); break;
        case ToolAction::StartSketchYZ: // Right: camera +X, up +Y
            enterSketchOnPlane(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 0, -1)))); break;
        case ToolAction::SketchOnFace: {
            const auto& sel = m_selection->getSelection();
            for (const auto& entry : sel) {
                if (entry.type == SelectionType::Face && !entry.shape.IsNull()) {
                    enterSketchOnFace(TopoDS::Face(entry.shape));
                    if (m_activeSketch) m_activeSketch->setSourceBody(entry.bodyId);
                    break;
                }
            }
            break;
        }
        case ToolAction::FinishSketch: {
            if (m_inSketchMode) {
                recordSketchMutation([&]{ m_sketchTool->onConfirm(); });
                exitSketchMode();
            }
            break;
        }
        case ToolAction::EditSketch: {
            const auto& sel = m_selection->getSelection();
            // Accept a selected whole sketch OR a selected sketch region (edit the
            // region's parent sketch) so "Edit Sketch" works from the region tools.
            for (const auto& entry : sel) {
                if ((entry.type == SelectionType::Sketch ||
                     entry.type == SelectionType::SketchRegion) && entry.sketchId >= 0) {
                    editSketch(entry.sketchId);
                    break;
                }
            }
            break;
        }
        case ToolAction::ExtrudeSketch: {
            const auto& sel = m_selection->getSelection();
            for (const auto& entry : sel) {
                if (entry.type == SelectionType::Sketch && entry.sketchId >= 0) {
                    extrudeSketchById(entry.sketchId);
                    break;
                }
            }
            break;
        }
        case ToolAction::PushPull: {
            beginPushPull();
            break;
        }
        case ToolAction::LookAtSketch: {
            alignCameraToActiveSketch();
            break;
        }
        case ToolAction::Line:
            if (m_inSketchMode) m_sketchTool->setMode(SketchToolMode::Line);
            break;
        case ToolAction::Circle:
            if (m_inSketchMode) m_sketchTool->setMode(SketchToolMode::Circle);
            break;
        case ToolAction::Rectangle:
            if (m_inSketchMode) m_sketchTool->setMode(SketchToolMode::Rectangle);
            break;
        case ToolAction::Arc:
            if (m_inSketchMode) m_sketchTool->setMode(SketchToolMode::Arc);
            break;
        case ToolAction::Spline:
            if (m_inSketchMode) m_sketchTool->setMode(SketchToolMode::Spline);
            break;
        case ToolAction::Polygon:
            if (m_inSketchMode) m_sketchTool->setMode(SketchToolMode::Polygon);
            break;
        case ToolAction::Trim:
            if (m_inSketchMode) m_sketchTool->setMode(SketchToolMode::Trim);
            break;
        case ToolAction::ResetCamera: m_viewport->getCamera().reset(); break;

        case ToolAction::Move: {
            if (!m_selection->hasSelectedBodies()) break;
            m_gizmo->setMode(GizmoMode::Translate);
            break;
        }

        case ToolAction::Extrude: {
            const auto& sel = m_selection->getSelection();
            if (m_selection->selectedFaceCount() >= 1) {
                for (const auto& entry : sel) {
                    if (entry.type == SelectionType::Face && !entry.shape.IsNull()) {
                        beginInteractiveExtrude(entry.shape);
                        break;
                    }
                }
            }
            break;
        }

        case ToolAction::Fillet: {
            if (m_selection->selectedEdgeCount() >= 1)
                beginInteractiveEdgeOp(EdgeOpType::Fillet);
            break;
        }

        case ToolAction::Chamfer: {
            if (m_selection->selectedEdgeCount() >= 1)
                beginInteractiveEdgeOp(EdgeOpType::Chamfer);
            break;
        }

        default: break;
    }
}

void Application::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();

    // Undo/Redo — use GLFW directly so it works even when ImGui has text input focus
    bool ctrlHeld = glfwGetKey(m_window->handle(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(m_window->handle(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    if (ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (!m_edgeOpActive && !m_extruding && !m_pushPullActive) {
            if (m_history->canUndo()) {
                m_history->undo(*m_document);
                m_selection->clear();
                m_hoveredBodyId = -1;
                m_meshesDirty = true;
            }
        }
    }
    if (ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
        if (!m_edgeOpActive && !m_extruding && !m_pushPullActive) {
            if (m_history->canRedo()) {
                m_history->redo(*m_document);
                m_selection->clear();
                m_hoveredBodyId = -1;
                m_meshesDirty = true;
            }
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K)) {
        m_commandPalette->toggle();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I)) {
        importStepFile();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E)) {
        exportStepFile();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        saveProject();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
        loadProject();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (m_gizmoDragging) {
            // Revert the body to where the drag started — same idea as cancelling
            // any other in-progress operation. Also cancel the gizmo's own drag
            // state so it doesn't keep dragging once the mouse moves again.
            try {
                if (m_gizmoDragBodyId >= 0 && !m_gizmoDragOriginalShape.IsNull()) {
                    m_document->updateBody(m_gizmoDragBodyId, m_gizmoDragOriginalShape);
                }
            } catch (...) {}
            m_gizmo->cancelDrag();
            m_gizmoDragging = false;
            m_gizmoDragOriginalShape.Nullify();
            m_gizmoDragBodyId = -1;
            m_gizmoTotalDelta = glm::vec3(0.0f);
            m_meshesDirty = true;
        } else if (m_pushPullActive) {
            cancelPushPull();
        } else if (m_edgeOpActive) {
            cancelInteractiveEdgeOp();
        } else if (m_extruding) {
            cancelInteractiveExtrude();
        } else if (m_inSketchMode) {
            m_sketchTool->onCancel();
            exitSketchMode();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && m_edgeOpActive) {
        m_edgeOpValue = static_cast<float>(std::atof(m_edgeOpInputBuf));
        updateInteractiveEdgeOp();
        commitInteractiveEdgeOp();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && m_extruding) {
        m_extrudeDistance = static_cast<float>(std::atof(m_extrudeInputBuf));
        updateInteractiveExtrude();
        commitInteractiveExtrude();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && m_pushPullActive) {
        m_pushPullDistance = static_cast<float>(std::atof(m_pushPullInputBuf));
        updatePushPull();
        commitPushPull();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        m_viewport->getCamera().reset();
    }
    // Delete selected body (through history for undo)
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_selection->hasSelection()) {
        const auto& sel = m_selection->getSelection();
        std::vector<int> bodiesToDelete;
        std::vector<int> sketchesToDelete;
        for (const auto& entry : sel) {
            // A selected sketch (or sketch region) deletes the whole sketch; a
            // body/face/edge selection deletes its body.
            if (entry.type == SelectionType::Sketch || entry.type == SelectionType::SketchRegion) {
                if (entry.sketchId >= 0) {
                    bool already = false;
                    for (int s : sketchesToDelete) { if (s == entry.sketchId) { already = true; break; } }
                    if (!already) sketchesToDelete.push_back(entry.sketchId);
                }
            } else if (entry.bodyId >= 0) {
                bool already = false;
                for (int b : bodiesToDelete) { if (b == entry.bodyId) { already = true; break; } }
                if (!already) bodiesToDelete.push_back(entry.bodyId);
            }
        }
        for (int bodyId : bodiesToDelete) {
            auto op = std::make_unique<DeleteOp>();
            op->setBodyId(bodyId);
            m_history->pushOperation(std::move(op), *m_document);
        }
        for (int sketchId : sketchesToDelete) {
            m_document->removeSketch(sketchId);
            markDirty();
        }
        m_selection->clear();
        m_hoveredBodyId = -1;
        m_meshesDirty = true;
    }
    // Gizmo mode switching
    if (!m_inSketchMode && !io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            m_gizmo->setMode(GizmoMode::Translate);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            m_gizmo->setMode(GizmoMode::Rotate);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            m_gizmo->setMode(GizmoMode::Scale);
        }
    }
}

void Application::rebuildMeshes() {
    m_shapeRenderer->clear();
    m_edgeRenderer->clear();
    auto ids = m_document->getAllBodyIds();
    int colorIdx = 0;
    for (int id : ids) {
        if (!m_document->isBodyVisible(id)) continue;
        const TopoDS_Shape& shape = m_document->getBody(id);
        int idx = m_shapeRenderer->tessellate(shape, 0.1f);
        if (idx >= 0) {
            m_shapeRenderer->setColor(idx, ShapeRenderer::bodyColor(colorIdx));
            colorIdx++;
        }
        m_edgeRenderer->addShape(shape, 0.1f);
    }
}

void Application::handleViewCubeAction(int action) {
    // ViewCube::render() has already moved the camera. For absolute-direction
    // actions (Front/Back/Top/Bottom/Left/Right and the corner views) we also
    // re-frame the model so it actually appears on screen from the new angle.
    // Rotate* actions are incremental — skip the zoom-fit there, it would yank
    // the target around mid-rotation.
    ViewCubeAction a = static_cast<ViewCubeAction>(action);
    switch (a) {
        case ViewCubeAction::RotateLeft:
        case ViewCubeAction::RotateRight:
        case ViewCubeAction::RotateUp:
        case ViewCubeAction::RotateDown:
            return;
        default:
            break;
    }

    Camera& cam = m_viewport->getCamera();

    Bnd_Box bbox;
    for (int id : m_document->getAllBodyIds()) {
        if (!m_document->isBodyVisible(id)) continue;
        try {
            BRepBndLib::Add(m_document->getBody(id), bbox);
        } catch (...) {}
    }
    if (!bbox.IsVoid()) {
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        cam.zoomToFit(
            glm::vec3(static_cast<float>(xmin), static_cast<float>(ymin), static_cast<float>(zmin)),
            glm::vec3(static_cast<float>(xmax), static_cast<float>(ymax), static_cast<float>(zmax)));
    }
}

void Application::saveProject() {
    FileDialogs::saveFile("Save Project", "project.materializr",
        {{"Materializr Project", "*.materializr"}},
        [this](const std::string& path) {
            if (path.empty()) return;
            auto result = ProjectIO::save(path, *m_document);
            if (result.success) {
                m_currentProjectPath = path;
                markSaved();
                std::fprintf(stdout, "Project saved to %s\n", path.c_str());
                if (m_closeAfterSave) m_confirmedClose = true;
            } else {
                std::fprintf(stderr, "Save failed: %s\n", result.errorMessage.c_str());
            }
            m_closeAfterSave = false;
        });
}

void Application::saveProjectQuick() {
    if (m_currentProjectPath.empty()) {
        saveProject();
        return;
    }
    auto result = ProjectIO::save(m_currentProjectPath, *m_document);
    if (result.success) {
        markSaved();
        std::fprintf(stdout, "Project saved to %s\n", m_currentProjectPath.c_str());
        if (m_closeAfterSave) m_confirmedClose = true;
    } else {
        std::fprintf(stderr, "Save failed: %s\n", result.errorMessage.c_str());
    }
    m_closeAfterSave = false;
}

void Application::loadProject() {
    FileDialogs::openFile("Open Project",
        {{"Materializr Project", "*.materializr"}},
        [this](const std::string& path) {
            if (path.empty()) return;
            m_document->clear();
            m_history->clear();
            m_selection->clear();
            auto result = ProjectIO::load(path, *m_document);
            if (result.success) {
                m_currentProjectPath = path;
                markSaved();
                m_meshesDirty = true;
                std::fprintf(stdout, "Loaded %d bodies from %s\n", result.bodiesLoaded, path.c_str());
            } else {
                std::fprintf(stderr, "Load failed: %s\n", result.errorMessage.c_str());
            }
        });
}

bool Application::isDirty() const {
    return (m_history && m_history->currentStep() != m_savedAtHistoryStep)
        || m_unsavedNonHistoryChanges;
}

void Application::markDirty() {
    m_unsavedNonHistoryChanges = true;
}

void Application::markSaved() {
    m_savedAtHistoryStep = m_history ? m_history->currentStep() : -1;
    m_unsavedNonHistoryChanges = false;
}

void Application::requestClose() {
    if (m_confirmedClose) return;
    if (!isDirty()) { m_confirmedClose = true; return; }
    m_showSavePrompt = true;
    m_closeAfterSave = false;
    glfwSetWindowShouldClose(m_window->handle(), GLFW_FALSE);
}

void Application::renderSavePrompt() {
    if (m_showSavePrompt) {
        ImGui::OpenPopup("Unsaved Changes");
        m_showSavePrompt = false; // OpenPopup latches; only call once per request
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have unsaved changes. Save before exiting?");
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(100, 0))) {
            m_closeAfterSave = true;
            saveProjectQuick();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(100, 0))) {
            m_confirmedClose = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_closeAfterSave = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::importStepFile() {
    FileDialogs::openFile("Import STEP",
        {{"STEP Files", "*.step *.stp *.STEP *.STP"}},
        [this](const std::string& path) {
            if (path.empty()) return;
            auto result = StepIO::import(path, *m_document);
            if (result.success) {
                m_meshesDirty = true;
                markDirty();
                std::fprintf(stdout, "Imported %d bodies from %s\n", result.bodiesImported, path.c_str());
            } else {
                std::fprintf(stderr, "Import failed: %s\n", result.errorMessage.c_str());
            }
        });
}

void Application::exportStepFile() {
    FileDialogs::saveFile("Export STEP", "export.step",
        {{"STEP Files", "*.step *.stp"}},
        [this](const std::string& path) {
            if (path.empty()) return;
            auto result = StepIO::exportFile(path, *m_document);
            if (result.success) std::fprintf(stdout, "Exported to %s\n", path.c_str());
            else std::fprintf(stderr, "Export failed: %s\n", result.errorMessage.c_str());
        });
}

void Application::exportStlFile() {
    FileDialogs::saveFile("Export STL", "export.stl",
        {{"STL Files", "*.stl"}},
        [this](const std::string& path) {
            if (path.empty()) return;
            auto result = StlExport::exportFile(path, *m_document);
            if (result.success) std::fprintf(stdout, "Exported %d triangles to %s\n", result.triangleCount, path.c_str());
            else std::fprintf(stderr, "STL export failed: %s\n", result.errorMessage.c_str());
        });
}

void Application::enterSketchMode() {
    // If a planar face is selected, route through enterSketchOnFace for consistency
    if (m_selection && m_selection->hasSelectedFaces()) {
        const auto& sel = m_selection->getSelection();
        for (const auto& entry : sel) {
            if (entry.type == SelectionType::Face && !entry.shape.IsNull()) {
                enterSketchOnFace(TopoDS::Face(entry.shape));
                return;
            }
        }
    }

    m_activeSketch = std::make_shared<Sketch>();
    m_sketchSolver = std::make_unique<SketchSolver>();
    m_activeSketchId = -1;

    m_sketchTool->setSketch(m_activeSketch.get());
    m_sketchTool->setSolver(m_sketchSolver.get());
    m_sketchTool->setMode(SketchToolMode::Line);
    m_inSketchMode = true;
    m_toolbar->setSketchMode(true);
    alignCameraToActiveSketch();
}

void Application::enterSketchOnPlane(const gp_Pln& plane) {
    // Start a fresh, freestanding sketch on a world base plane (no source face),
    // so the user can model from scratch with no existing body. Drawing tools,
    // the adjustable grid and the ortho camera all come from the shared sketch
    // path via alignCameraToActiveSketch() / renderSketchTools().
    m_activeSketch = std::make_shared<Sketch>();
    m_sketchSolver = std::make_unique<SketchSolver>();
    m_activeSketchId = -1;

    m_activeSketch->setPlane(plane);

    m_sketchTool->setSketch(m_activeSketch.get());
    m_sketchTool->setSolver(m_sketchSolver.get());
    m_sketchTool->setMode(SketchToolMode::Line);
    m_inSketchMode = true;
    m_toolbar->setSketchMode(true);
    alignCameraToActiveSketch();
}

void Application::enterSketchOnFace(const TopoDS_Face& face) {
    m_activeSketch = std::make_shared<Sketch>();
    m_sketchSolver = std::make_unique<SketchSolver>();
    m_activeSketchId = -1;

    Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
    if (!surf.IsNull() && surf->IsKind(STANDARD_TYPE(Geom_Plane))) {
        Handle(Geom_Plane) geomPlane = Handle(Geom_Plane)::DownCast(surf);
        m_activeSketch->setPlane(geomPlane->Pln());
        m_activeSketch->setSourceFace(face);
    } else {
        // Fallback to default XY plane if face is non-planar
        m_activeSketch->setPlane(gp_Pln(gp_Pnt(0,0,0), gp_Dir(0,0,1)));
        std::fprintf(stderr, "Selected face is not planar; using XY plane instead\n");
    }

    m_sketchTool->setSketch(m_activeSketch.get());
    m_sketchTool->setSolver(m_sketchSolver.get());
    m_sketchTool->setMode(SketchToolMode::Line);
    m_inSketchMode = true;
    m_toolbar->setSketchMode(true);
    alignCameraToActiveSketch();
}

void Application::recordSketchMutation(const std::function<void()>& mutator) {
    if (!m_activeSketch) { mutator(); return; }
    // Signature includes counts AND element IDs so that swaps (trim line→line,
    // trim circle→arc) register as a mutation even though counts may be equal.
    auto signature = [](const Sketch& s) {
        size_t h = 1469598103934665603ull;
        auto mix = [&](size_t v) { h = (h ^ v) * 1099511628211ull; };
        mix(s.getLines().size());
        for (const auto& l : s.getLines()) mix(static_cast<size_t>(l.id));
        mix(s.getCircles().size());
        for (const auto& c : s.getCircles()) mix(static_cast<size_t>(c.id));
        mix(s.getArcs().size());
        for (const auto& a : s.getArcs()) mix(static_cast<size_t>(a.id));
        mix(s.getSplines().size());
        for (const auto& sp : s.getSplines()) mix(static_cast<size_t>(sp.id));
        mix(s.getPolygons().size());
        for (const auto& p : s.getPolygons()) mix(static_cast<size_t>(p.id));
        return h;
    };
    size_t beforeSig = signature(*m_activeSketch);
    auto before = std::make_shared<Sketch>(*m_activeSketch);
    mutator();
    size_t afterSig = signature(*m_activeSketch);
    if (afterSig == beforeSig) return; // nothing structural changed → no history step
    auto after = std::make_shared<Sketch>(*m_activeSketch);
    auto op = std::make_unique<SketchEditOp>(m_activeSketch, std::move(before), std::move(after));
    m_history->pushExecuted(std::move(op));
}

void Application::editSketch(int sketchId) {
    auto sketch = m_document->getSketch(sketchId);
    if (!sketch) return;

    m_activeSketch = sketch; // shared ownership - edits go straight to the stored sketch
    m_sketchSolver = std::make_unique<SketchSolver>();
    m_activeSketchId = sketchId;

    m_sketchTool->setSketch(m_activeSketch.get());
    m_sketchTool->setSolver(m_sketchSolver.get());
    m_sketchTool->setMode(SketchToolMode::Line);
    m_inSketchMode = true;
    m_toolbar->setSketchMode(true);
    m_selection->clear();
    alignCameraToActiveSketch();
}

void Application::extrudeSketchById(int sketchId) {
    auto sketch = m_document->getSketch(sketchId);
    if (!sketch) return;
    TopoDS_Face profile = buildSketchProfileFace(*sketch);
    if (profile.IsNull()) {
        std::fprintf(stderr, "Sketch has no closed profile to extrude\n");
        return;
    }
    beginInteractiveExtrude(profile);
}

void Application::alignCameraToActiveSketch() {
    if (!m_activeSketch || !m_viewport) return;

    const gp_Pln& pln = m_activeSketch->getPlane();
    const gp_Ax3& ax = pln.Position();
    gp_Pnt o = ax.Location();
    gp_Dir n = ax.Direction();
    gp_Dir y = ax.YDirection();

    glm::vec3 planeOrigin(static_cast<float>(o.X()), static_cast<float>(o.Y()), static_cast<float>(o.Z()));
    glm::vec3 normal(static_cast<float>(n.X()), static_cast<float>(n.Y()), static_cast<float>(n.Z()));
    glm::vec3 up(static_cast<float>(y.X()), static_cast<float>(y.Y()), static_cast<float>(y.Z()));

    // Pick an ortho size that frames the source face if present, otherwise default
    // to something readable. Use the current sketch grid step's scale as a hint.
    float orthoSize = std::max(20.0f, m_sketchGridStep * 40.0f);
    if (!m_activeSketch->getSourceFace().IsNull()) {
        try {
            Bnd_Box bb;
            BRepBndLib::Add(m_activeSketch->getSourceFace(), bb);
            if (!bb.IsVoid()) {
                double xmin, ymin, zmin, xmax, ymax, zmax;
                bb.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                float dx = static_cast<float>(xmax - xmin);
                float dy = static_cast<float>(ymax - ymin);
                float dz = static_cast<float>(zmax - zmin);
                float diag = 0.5f * std::sqrt(dx*dx + dy*dy + dz*dz);
                if (diag > 1e-3f) orthoSize = diag * 1.2f;
            }
        } catch (...) {}
    }

    Camera& cam = m_viewport->getCamera();
    float standoff = std::max(orthoSize * 4.0f, 10.0f);
    cam.setTarget(planeOrigin);
    cam.setPosition(planeOrigin + normal * standoff);
    cam.setUp(up);
    cam.setOrthoSize(orthoSize);
    cam.setOrthographic(true);
}

TopoDS_Face Application::buildSketchProfileFace(const Sketch& sketch) const {
    auto wires = sketch.buildWires();
    if (wires.empty()) return TopoDS_Face();

    // Pick the wire with the largest 3D bbox diagonal as the outer; the rest are holes.
    // This produces a single face with holes so a prism over a "ring" sketch becomes a tube.
    int outerIdx = 0;
    double bestExtent = -1.0;
    std::vector<double> extents(wires.size(), 0.0);
    for (size_t i = 0; i < wires.size(); ++i) {
        Bnd_Box bb;
        BRepBndLib::Add(wires[i], bb);
        if (bb.IsVoid()) continue;
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bb.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
        double diag = dx*dx + dy*dy + dz*dz;
        extents[i] = diag;
        if (diag > bestExtent) {
            bestExtent = diag;
            outerIdx = static_cast<int>(i);
        }
    }

    BRepBuilderAPI_MakeFace faceMaker(sketch.getPlane(), wires[outerIdx]);
    if (!faceMaker.IsDone()) return TopoDS_Face();

    for (size_t i = 0; i < wires.size(); ++i) {
        if (static_cast<int>(i) == outerIdx) continue;
        // Reverse inner wire orientation so it acts as a hole
        TopoDS_Wire inner = TopoDS::Wire(wires[i].Reversed());
        faceMaker.Add(inner);
    }
    faceMaker.Build();
    if (!faceMaker.IsDone()) return TopoDS_Face();
    return faceMaker.Face();
}

glm::vec2 Application::screenToSketch(float sx, float sy, float vpW, float vpH) {
    Camera& cam = m_viewport->getCamera();
    glm::mat4 view = cam.getViewMatrix();
    glm::mat4 proj = cam.getProjectionMatrix();
    glm::mat4 invVP = glm::inverse(proj * view);

    // Normalize to [-1,1]
    float nx = (sx / vpW) * 2.0f - 1.0f;
    float ny = 1.0f - (sy / vpH) * 2.0f;

    // Unproject near and far points
    glm::vec4 nearPt = invVP * glm::vec4(nx, ny, -1.0f, 1.0f);
    glm::vec4 farPt = invVP * glm::vec4(nx, ny, 1.0f, 1.0f);
    nearPt /= nearPt.w;
    farPt /= farPt.w;

    glm::vec3 rayOrigin(nearPt);
    glm::vec3 rayDir = glm::normalize(glm::vec3(farPt) - glm::vec3(nearPt));

    // Intersect ray with sketch plane
    const gp_Pln& pln = m_activeSketch->getPlane();
    const gp_Ax3& ax = pln.Position();
    glm::vec3 planeOrigin(ax.Location().X(), ax.Location().Y(), ax.Location().Z());
    glm::vec3 planeNormal(ax.Direction().X(), ax.Direction().Y(), ax.Direction().Z());
    glm::vec3 planeX(ax.XDirection().X(), ax.XDirection().Y(), ax.XDirection().Z());
    glm::vec3 planeY(ax.YDirection().X(), ax.YDirection().Y(), ax.YDirection().Z());

    float denom = glm::dot(rayDir, planeNormal);
    if (std::abs(denom) < 1e-8f) return glm::vec2(0);

    float t = glm::dot(planeOrigin - rayOrigin, planeNormal) / denom;
    glm::vec3 hitPoint = rayOrigin + rayDir * t;

    // Project hit point onto sketch plane's 2D coordinate system
    glm::vec3 local = hitPoint - planeOrigin;
    return glm::vec2(glm::dot(local, planeX), glm::dot(local, planeY));
}

void Application::beginInteractiveEdgeOp(EdgeOpType type) {
    const auto& sel = m_selection->getSelection();
    int bodyId = -1;
    std::vector<TopoDS_Shape> edges;
    for (const auto& entry : sel) {
        if (entry.type == SelectionType::Edge && !entry.shape.IsNull()) {
            if (bodyId < 0) bodyId = entry.bodyId;
            if (entry.bodyId == bodyId) edges.push_back(entry.shape);
        }
    }
    if (bodyId < 0 || edges.empty()) return;

    m_edgeOpType = type;
    m_edgeOpActive = true;
    m_edgeOpBodyId = bodyId;
    m_edgeOpEdges = edges;
    m_edgeOpValue = 1.0f;
    std::snprintf(m_edgeOpInputBuf, sizeof(m_edgeOpInputBuf), "%.1f", m_edgeOpValue);
    m_edgeOpInputFocus = true;

    // Save original shape for live preview
    try {
        m_edgeOpPreviousShape = m_document->getBody(bodyId);
    } catch (...) { m_edgeOpActive = false; return; }

    updateInteractiveEdgeOp();
}

void Application::updateInteractiveEdgeOp() {
    if (!m_edgeOpActive || m_edgeOpBodyId < 0) return;
    if (m_edgeOpValue < 0.01f) return;

    // Restore original shape before re-applying
    m_document->updateBody(m_edgeOpBodyId, m_edgeOpPreviousShape);

    try {
        if (m_edgeOpType == EdgeOpType::Fillet) {
            auto op = std::make_unique<FilletOp>();
            op->setBody(m_edgeOpBodyId);
            std::vector<TopoDS_Edge> typedEdges;
            for (const auto& e : m_edgeOpEdges) typedEdges.push_back(TopoDS::Edge(e));
            op->setEdges(typedEdges);
            op->setRadius(static_cast<double>(m_edgeOpValue));
            if (op->execute(*m_document)) {
                m_meshesDirty = true;
            } else {
                // Failed — restore original
                m_document->updateBody(m_edgeOpBodyId, m_edgeOpPreviousShape);
            }
        } else {
            auto op = std::make_unique<ChamferOp>();
            op->setBody(m_edgeOpBodyId);
            std::vector<TopoDS_Edge> typedEdges;
            for (const auto& e : m_edgeOpEdges) typedEdges.push_back(TopoDS::Edge(e));
            op->setEdges(typedEdges);
            op->setDistance(static_cast<double>(m_edgeOpValue));
            if (op->execute(*m_document)) {
                m_meshesDirty = true;
            } else {
                m_document->updateBody(m_edgeOpBodyId, m_edgeOpPreviousShape);
            }
        }
    } catch (...) {
        m_document->updateBody(m_edgeOpBodyId, m_edgeOpPreviousShape);
    }
}

void Application::commitInteractiveEdgeOp() {
    // Restore original, then do it properly through history
    m_document->updateBody(m_edgeOpBodyId, m_edgeOpPreviousShape);

    if (m_edgeOpType == EdgeOpType::Fillet) {
        auto op = std::make_unique<FilletOp>();
        op->setBody(m_edgeOpBodyId);
        std::vector<TopoDS_Edge> typedEdges;
        for (const auto& e : m_edgeOpEdges) typedEdges.push_back(TopoDS::Edge(e));
        op->setEdges(typedEdges);
        op->setRadius(static_cast<double>(m_edgeOpValue));
        m_history->pushOperation(std::move(op), *m_document);
    } else {
        auto op = std::make_unique<ChamferOp>();
        op->setBody(m_edgeOpBodyId);
        std::vector<TopoDS_Edge> typedEdges;
        for (const auto& e : m_edgeOpEdges) typedEdges.push_back(TopoDS::Edge(e));
        op->setEdges(typedEdges);
        op->setDistance(static_cast<double>(m_edgeOpValue));
        m_history->pushOperation(std::move(op), *m_document);
    }

    m_edgeOpActive = false;
    m_edgeOpEdges.clear();
    m_edgeOpPreviousShape.Nullify();
    m_selection->clear();
    m_meshesDirty = true;
    std::fprintf(stdout, "%s %.1f mm committed\n",
                 m_edgeOpType == EdgeOpType::Fillet ? "Fillet" : "Chamfer", m_edgeOpValue);
    m_edgeOpType = EdgeOpType::None;
}

void Application::cancelInteractiveEdgeOp() {
    if (m_edgeOpBodyId >= 0 && !m_edgeOpPreviousShape.IsNull()) {
        m_document->updateBody(m_edgeOpBodyId, m_edgeOpPreviousShape);
    }
    m_edgeOpActive = false;
    m_edgeOpEdges.clear();
    m_edgeOpPreviousShape.Nullify();
    m_edgeOpType = EdgeOpType::None;
    m_meshesDirty = true;
}

void Application::beginInteractiveExtrude(const TopoDS_Shape& profile) {
    m_extrudeProfile = profile;
    m_extruding = true;
    m_extrudeDistance = 5.0f;
    std::snprintf(m_extrudeInputBuf, sizeof(m_extrudeInputBuf), "%.1f", m_extrudeDistance);
    m_extrudeInputFocus = true;

    // Compute face normal and center
    if (profile.ShapeType() == TopAbs_FACE) {
        BRepGProp_Face prop(TopoDS::Face(profile));
        gp_Pnt center;
        gp_Vec norm;
        double u1, u2, v1, v2;
        prop.Bounds(u1, u2, v1, v2);
        prop.Normal((u1 + u2) * 0.5, (v1 + v2) * 0.5, center, norm);
        if (norm.Magnitude() > 1e-10) {
            m_extrudeNormal = glm::vec3(norm.X(), norm.Y(), norm.Z());
            m_extrudeNormal = glm::normalize(m_extrudeNormal);
        }
        m_extrudeOrigin = glm::vec3(center.X(), center.Y(), center.Z());
    }

    // Create initial preview body
    auto op = std::make_unique<ExtrudeOp>();
    op->setProfile(profile);
    op->setDistance(m_extrudeDistance);
    op->setMode(ExtrudeMode::NewBody);
    if (m_history->pushOperation(std::move(op), *m_document)) {
        auto ids = m_document->getAllBodyIds();
        m_extrudePreviewBodyId = ids.back();
        m_meshesDirty = true;
    }
}

void Application::updateInteractiveExtrude() {
    if (!m_extruding || m_extrudePreviewBodyId < 0) return;

    // Remove old preview and create new one at current distance
    m_document->removeBody(m_extrudePreviewBodyId);
    m_history->undo(*m_document); // undo the last extrude

    auto op = std::make_unique<ExtrudeOp>();
    op->setProfile(m_extrudeProfile);
    op->setDistance(static_cast<double>(m_extrudeDistance));
    op->setMode(ExtrudeMode::NewBody);
    if (m_history->pushOperation(std::move(op), *m_document)) {
        auto ids = m_document->getAllBodyIds();
        m_extrudePreviewBodyId = ids.back();
        m_meshesDirty = true;
    }
}

void Application::commitInteractiveExtrude() {
    // The current extrude is already in history — just finalize
    m_extruding = false;
    m_extrudeProfile.Nullify();
    m_extrudePreviewBodyId = -1;
    m_meshesDirty = true;
    std::fprintf(stdout, "Extruded %.1f mm\n", m_extrudeDistance);
}

void Application::cancelInteractiveExtrude() {
    if (m_extrudePreviewBodyId >= 0) {
        m_document->removeBody(m_extrudePreviewBodyId);
        m_history->undo(*m_document);
    }
    m_extruding = false;
    m_extrudeProfile.Nullify();
    m_extrudePreviewBodyId = -1;
    m_meshesDirty = true;
}

Application::SketchRegionHit Application::pickSketchRegion(float screenX, float screenY,
                                                           float vpW, float vpH) const {
    SketchRegionHit hit;
    if (!m_document || !m_viewport) return hit;

    const Camera& cam = m_viewport->getCamera();
    glm::mat4 view = cam.getViewMatrix();
    glm::mat4 proj = cam.getProjectionMatrix();
    glm::mat4 invVP = glm::inverse(proj * view);

    // Build a world-space ray through a given pixel.
    auto rayAt = [&](float sx, float sy, glm::vec3& origin, glm::vec3& dir) {
        float ndcx = (sx / vpW) * 2.0f - 1.0f;
        float ndcy = 1.0f - (sy / vpH) * 2.0f;
        glm::vec4 n = invVP * glm::vec4(ndcx, ndcy, -1.0f, 1.0f);
        glm::vec4 f = invVP * glm::vec4(ndcx, ndcy, 1.0f, 1.0f);
        n /= n.w; f /= f.w;
        origin = glm::vec3(n);
        dir = glm::normalize(glm::vec3(f) - glm::vec3(n));
    };

    glm::vec3 rayOrigin, rayDir;
    rayAt(screenX, screenY, rayOrigin, rayDir);

    float bestT = std::numeric_limits<float>::infinity();

    auto testSketch = [&](int sketchId, const Sketch& sketch) {
        const gp_Pln& pln = sketch.getPlane();
        const gp_Ax3& ax = pln.Position();
        glm::vec3 planeOrigin(ax.Location().X(), ax.Location().Y(), ax.Location().Z());
        glm::vec3 planeNormal(ax.Direction().X(), ax.Direction().Y(), ax.Direction().Z());
        glm::vec3 planeX(ax.XDirection().X(), ax.XDirection().Y(), ax.XDirection().Z());
        glm::vec3 planeY(ax.YDirection().X(), ax.YDirection().Y(), ax.YDirection().Z());

        auto projectToPlane = [&](glm::vec3 o, glm::vec3 d, float& tOut, glm::vec2& p2dOut) -> bool {
            float denom = glm::dot(d, planeNormal);
            if (std::abs(denom) < 1e-8f) return false;
            float t = glm::dot(planeOrigin - o, planeNormal) / denom;
            if (t <= 0.0f) return false;
            glm::vec3 local = (o + d * t) - planeOrigin;
            tOut = t;
            p2dOut = glm::vec2(glm::dot(local, planeX), glm::dot(local, planeY));
            return true;
        };

        float t;
        glm::vec2 p2d;
        if (!projectToPlane(rayOrigin, rayDir, t, p2d)) return;
        if (t >= bestT) return;

        // Screen-space pick tolerance: how far ~6px maps to on this plane, so the
        // boundary catch area is a consistent, comfortable width at any zoom.
        float tol = 0.0f;
        glm::vec3 o2, d2; float t2; glm::vec2 p2d2;
        rayAt(screenX + 6.0f, screenY, o2, d2);
        if (projectToPlane(o2, d2, t2, p2d2)) tol = glm::length(p2d2 - p2d);

        auto regions = sketch.buildRegions();
        for (size_t i = 0; i < regions.size(); ++i) {
            if (sketch.isPointInOrNearRegion(regions[i], p2d, tol)) {
                bestT = t;
                hit.sketchId = sketchId;
                hit.regionIndex = static_cast<int>(i);
                hit.worldPoint = rayOrigin + rayDir * t;
                break; // first match per sketch is fine; nesting handled by the test
            }
        }
    };

    // Test the active sketch first (most relevant when in sketch mode)
    if (m_activeSketch) testSketch(m_activeSketchId, *m_activeSketch);
    // Then all stored sketches
    for (int sid : m_document->getAllSketchIds()) {
        if (!m_document->isSketchVisible(sid)) continue;
        if (sid == m_activeSketchId) continue;
        auto sk = m_document->getSketch(sid);
        if (sk) testSketch(sid, *sk);
    }

    return hit;
}

void Application::beginPushPull() {
    m_pushPullTargets.clear();
    m_pushPullPreviewBodyIds.clear();
    m_pushPullPreviousBodies.clear();
    m_pushPullPreviewPushed = false;

    // Gather all selected SketchRegion entries AND body face selections.
    for (const auto& e : m_selection->getSelection()) {
        if (e.type == SelectionType::SketchRegion) {
            auto sketch = m_document->getSketch(e.sketchId);
            if (!sketch) continue;
            auto regions = sketch->buildRegions();
            if (e.subShapeIndex < 0 || e.subShapeIndex >= static_cast<int>(regions.size())) continue;
            PushPullTarget t;
            t.sketchId = e.sketchId;
            t.regionIndex = e.subShapeIndex;
            t.sourceBodyId = sketch->getSourceBody();
            t.profile = regions[e.subShapeIndex].face;
            if (t.profile.IsNull()) continue;
            m_pushPullTargets.push_back(t);
        } else if (e.type == SelectionType::Face && !e.shape.IsNull()) {
            // Push/Pull on a body face: face is the profile, the owning body is the source.
            // Positive distance extrudes outward (Fuse), negative cuts inward (Cut).
            PushPullTarget t;
            t.sketchId = -1;
            t.regionIndex = -1;
            t.sourceBodyId = e.bodyId;
            t.profile = TopoDS::Face(e.shape);
            if (t.profile.IsNull()) continue;
            m_pushPullTargets.push_back(t);
        }
    }

    if (m_pushPullTargets.empty()) {
        std::fprintf(stderr, "Push/Pull: select a sketch region or a body face first\n");
        return;
    }

    m_pushPullActive = true;
    m_pushPullDistance = 5.0f;
    std::snprintf(m_pushPullInputBuf, sizeof(m_pushPullInputBuf), "%.1f", m_pushPullDistance);
    m_pushPullInputFocus = true;

    updatePushPull();
}

void Application::updatePushPull() {
    if (!m_pushPullActive) return;

    // Only undo OUR previous preview — not any other pushpull that may already be
    // committed at the top of the history.
    if (m_pushPullPreviewPushed && m_history->canUndo()) {
        m_history->undo(*m_document);
        m_pushPullPreviewPushed = false;
    }

    auto op = std::make_unique<PushPullOp>();
    std::vector<PushPullOp::Target> targets;
    for (const auto& t : m_pushPullTargets) {
        PushPullOp::Target ot;
        ot.profile = t.profile;
        ot.sourceBodyId = t.sourceBodyId;
        targets.push_back(ot);
    }
    op->setTargets(std::move(targets));
    op->setDistance(static_cast<double>(m_pushPullDistance));
    if (m_history->pushOperation(std::move(op), *m_document)) {
        m_pushPullPreviewPushed = true;
    }
    m_meshesDirty = true;
}

void Application::commitPushPull() {
    // The last preview push IS the final state — just clean up
    m_pushPullActive = false;
    m_pushPullPreviewPushed = false;
    m_pushPullTargets.clear();
    m_meshesDirty = true;
    m_selection->clear();
    std::fprintf(stdout, "Push/Pull committed at %.2f mm\n", m_pushPullDistance);
}

void Application::cancelPushPull() {
    if (!m_pushPullActive) return;
    if (m_pushPullPreviewPushed && m_history->canUndo()) {
        m_history->undo(*m_document);
        m_pushPullPreviewPushed = false;
    }
    m_pushPullActive = false;
    m_pushPullTargets.clear();
    m_meshesDirty = true;
}

void Application::exitSketchMode() {
    m_inSketchMode = false;
    m_toolbar->setSketchMode(false);
    m_sketchTool->setMode(SketchToolMode::None);
    m_sketchTool->setSketch(nullptr);
    m_sketchTool->setSolver(nullptr);

    // Persist the sketch into the document if it has any geometry. New sketches get added;
    // edits to existing sketches are already reflected via the shared_ptr.
    if (m_activeSketch && m_activeSketch->elementCount() > 0) {
        if (m_activeSketchId < 0) {
            m_activeSketchId = m_document->addSketch(m_activeSketch);
            markDirty();
            std::fprintf(stdout, "Sketch saved (id %d)\n", m_activeSketchId);
        }
    } else if (m_activeSketchId < 0) {
        std::fprintf(stdout, "Sketch discarded (empty)\n");
    }

    m_activeSketch.reset();
    m_sketchSolver.reset();
    m_activeSketchId = -1;
    m_meshesDirty = true; // refresh sketch rendering set
}

void Application::run() {
    while (true) {
        m_window->pollEvents();

        // The save-prompt's Don't Save / post-save-success path sets this flag
        // directly. Check it every frame so we exit without requiring the user
        // to click the X a second time.
        if (m_confirmedClose) break;

        // Intercept window-close requests: if there are unsaved changes, show
        // the prompt and cancel the close until the user picks Save/Don't Save.
        if (m_window->shouldClose()) {
            requestClose();
            if (m_confirmedClose) break;
        }

        beginFrame();
        renderDockspace();
        renderMenuBar();

        if (m_renderersReady) {
            renderViewport();

            m_toolbar->setGridStep(m_sketchGridStep);
            m_toolbar->setSnapToGrid(m_snapToGrid);
            m_toolbar->setCameraOrtho(m_viewport->getCamera().isOrthographic());
            ToolAction action = m_toolbar->render();
            m_sketchGridStep = m_toolbar->getGridStep();
            m_snapToGrid = m_toolbar->getSnapToGrid();
            if (action != ToolAction::None) {
                handleToolAction(static_cast<int>(action));
            }

            // Active interactive tool (plugin system)
            if (auto* tool = PluginRegistry::instance().activeTool()) {
                if (!tool->update(*m_pluginContext)) {
                    // The tool finished (it ran its own commit()/cancel()); just
                    // clear it. Do NOT call deactivateTool() here — that cancels,
                    // which would undo a just-committed operation (e.g. push/pull).
                    PluginRegistry::instance().finishActiveTool();
                } else {
                    tool->renderOverlay(*m_pluginContext);
                }
            }

            renderInteractionsPanel();
            renderSettings();

            if (m_historyPanel->render()) {
                m_meshesDirty = true;
            }

            if (m_itemsPanel->render()) {
                m_hoveredBodyId = -1;
                m_meshesDirty = true;
            }
            if (m_propertiesPanel->render()) {
                m_meshesDirty = true;
            }
            m_statusBar->setSketchMode(m_inSketchMode);
            m_statusBar->render();
            m_commandPalette->render();
            FileDialogs::render();
            renderSavePrompt();

            handleShortcuts();
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        endFrame();

        m_window->swapBuffers();
    }
}

} // namespace materializr
