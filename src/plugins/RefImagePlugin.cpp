#include "../plugin/PluginMacro.h"
#include "../plugin/PluginContext.h"
#include "../core/Document.h"
#include "../core/Events.h"
#include "../core/EventBus.h"
#include "../core/SelectionManager.h"
#include "../viewport/RefImageRenderer.h"
#include "../viewport/Camera.h"

#include <memory>

// Reference-image canvas, owned end-to-end by this plugin (same architecture
// as ConstructionPlanePlugin):
//   - the toolbar button + command that kick Application's import flow
//     (file dialog + document mutation need deep Application access)
//   - the RefImageRenderer that draws each photo as a textured quad at its
//     host construction plane's pose
//   - the render pass that re-syncs the draw list when planes change
//
// A reference image IS a construction plane (plus a raster payload keyed by
// the plane's id in Document), so selection, the move/rotate gizmo,
// visibility, the Items panel, and sketch-on-plane all come from the existing
// plane plumbing. ConstructionPlanePlugin skips image-host planes so the blue
// quad doesn't draw over the photo.
namespace {

struct RefImageState {
    materializr::RefImageRenderer renderer;
    bool dirty = true;
};

static std::unique_ptr<RefImageState> g_state;

} // namespace

REGISTER_PLUGIN(RefImage, [](materializr::PluginContext& ctx) {
    auto action = [](materializr::PluginContext& c) {
        c.requestInteractiveOp(materializr::InteractiveOp::ImportRefImage);
    };
    ctx.registerToolbarButton({"Reference Image", "Create",
        materializr::SelectionContext::Always, 55,
        action, nullptr,
        "Import a photo as a traceable underlay on a construction plane. "
        "Photograph the object with a ruler in the shot, calibrate the scale "
        "off the ruler, then sketch over it — move/rotate the plane like any "
        "construction plane."});
    ctx.registerCommand({"Import Reference Image", "", action});

    // Same three plane events as the plane renderer — pose moves, renames,
    // visibility, AND every image-side change (opacity/size/add/remove) ride
    // PlaneChangedEvent, so one dirty flag covers everything.
    ctx.events().subscribe<materializr::PlaneAddedEvent>(
        [](const materializr::PlaneAddedEvent&) {
            if (g_state) g_state->dirty = true;
        });
    ctx.events().subscribe<materializr::PlaneRemovedEvent>(
        [](const materializr::PlaneRemovedEvent&) {
            if (g_state) g_state->dirty = true;
        });
    ctx.events().subscribe<materializr::PlaneChangedEvent>(
        [](const materializr::PlaneChangedEvent&) {
            if (g_state) g_state->dirty = true;
        });
    // Tab switch: these textured quads belong to the outgoing document (see
    // the same subscription in ConstructionPlanePlugin).
    ctx.events().subscribe<materializr::ActiveDocumentChangedEvent>(
        [](const materializr::ActiveDocumentChangedEvent&) {
            if (g_state) g_state->dirty = true;
        });

    materializr::RenderPassContribution pass;
    pass.name = "ReferenceImages";
    pass.priority = 490; // just under construction planes (500): photo first,
                         // so a plain plane's translucent fill can overlay it
    pass.initialize = []() -> bool {
        if (!g_state) g_state = std::make_unique<RefImageState>();
        return g_state->renderer.initialize();
    };
    pass.render = [](materializr::PluginContext& c,
                     const glm::mat4& view, const glm::mat4& proj) {
        if (!g_state) return;
        // Unlike construction planes, the image STAYS visible while sketching
        // in ortho — tracing over the photo is the whole point of the feature.
        int selectedPlaneId = -1;
        for (const auto& sel : c.selection().getSelection()) {
            if (sel.type == SelectionType::Plane && sel.planeId >= 0) {
                selectedPlaneId = sel.planeId; break;
            }
        }
        static int s_lastSelected = -2;
        if (g_state->dirty || selectedPlaneId != s_lastSelected) {
            auto& doc = c.document();
            std::vector<materializr::RefImageRenderer::Item> items;
            for (int pid : doc.getAllRefImagePlaneIds()) {
                const auto* img = doc.getRefImage(pid);
                const auto* plane = doc.getPlane(pid);
                if (!img || !plane || !plane->visible) continue;
                materializr::RefImageRenderer::Item it;
                it.planeId = pid;
                it.plane = plane->plane;
                it.widthMM = img->widthMM;
                it.heightMM = (img->pixW > 0)
                    ? img->widthMM * static_cast<double>(img->pixH) / img->pixW
                    : img->widthMM;
                it.opacity = img->opacity;
                it.selected = (pid == selectedPlaneId);
                it.fileBytes = &img->fileBytes;
                items.push_back(it);
            }
            g_state->renderer.sync(items);
            g_state->dirty = false;
            s_lastSelected = selectedPlaneId;
        }
        g_state->renderer.render(view, proj);
    };
    ctx.registerRenderPass(std::move(pass));
})
