#pragma once
#include "Contributions.h"
#include <memory>
#include <string>

class Document;
class History;
class SelectionManager;

namespace materializr {

class EventBus;
class Camera;
class InteractiveTool;

class PluginContext {
public:
    Document& document();
    History& history();
    SelectionManager& selection();
    EventBus& events();
    const Camera& camera() const;

    void markMeshesDirty();

    // True while the host Application has the sketch editor active. Plugins
    // can use this to suppress decorations that would clutter the sketch
    // canvas — the Construction Plane plugin hides plane quads when
    // sketching in ortho so the user has a clean drawing surface.
    bool isInSketchMode() const;

    // Request that the host Application start an interactive popup-driven op
    // (which the plugin can't run on its own — those need viewport + UI plumbing
    // that lives in Application). The caller passes a short string identifying
    // which op; Application picks it up via takeRequestedInteractiveOp() once
    // per frame and dispatches. Examples: "LinearPattern", "RadialPattern".
    // Calling this from a toolbar action defers the actual popup to the next
    // frame, which is exactly when Application checks for it.
    void requestInteractiveOp(const std::string& name);
    std::string takeRequestedInteractiveOp();

    void registerToolbarButton(ToolbarContribution contrib);
    void registerCommand(CommandContribution contrib);
    void registerMenuItem(MenuContribution contrib);
    void registerIOFormat(IOFormatContribution contrib);
    void registerRenderPass(RenderPassContribution contrib);
    void registerPropertySection(PropertyContribution contrib);

    void _bind(Document* doc, History* hist, SelectionManager* sel,
               EventBus* bus, Camera* cam, bool* meshesDirtyFlag,
               const bool* sketchModeFlag);

private:
    Document* m_document = nullptr;
    History* m_history = nullptr;
    SelectionManager* m_selection = nullptr;
    EventBus* m_eventBus = nullptr;
    Camera* m_camera = nullptr;
    bool* m_meshesDirtyFlag = nullptr;
    const bool* m_sketchModeFlag = nullptr;
    std::string m_pendingInteractiveOp;
};

} // namespace materializr
