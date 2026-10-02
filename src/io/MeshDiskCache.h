#pragma once
// On-disk cache of display meshes, consulted only while a project loads.
//
// A project file stores bodies WITHOUT triangulation (megabytes per body), so
// every open re-meshed everything - 16 s on autumn.mzr at Ultra, all of it on
// the UI thread. Opening the same file again gives byte-identical shapes, so
// the mesh from last time is still exactly right: key it by the body's own
// serialized geometry plus the quality setting, and hand it back.
//
// Load-only on purpose. An edit always produces a shape no cache entry could
// match, and undo/redo already keeps its meshes in memory (ShapeRenderer's
// m_meshedAtPrev), so a lookup anywhere else is pure overhead. The scope flag
// is set around Application::loadProjectWithProgress's tessellation pass.
#include <TopoDS_Shape.hxx>

namespace materializr {
namespace meshcache {

// Store only meshes that cost at least this much to make; anything cheaper is
// faster to re-mesh than to hash, write and read back.
constexpr double kStoreMinMs = 250.0;

bool active();

// Marks the load's tessellation pass. Nests; thread-safe to read.
struct LoadScope {
    LoadScope();
    ~LoadScope();
    LoadScope(const LoadScope&) = delete;
    LoadScope& operator=(const LoadScope&) = delete;
};

// Puts the cached triangulation for exactly this shape and quality onto it
// (faces and edge polygons). False on a miss or any mismatch - the caller
// then meshes as usual. Expects a shape with no triangulation (Cleaned).
bool load(const TopoDS_Shape& shape, double deflection, double angularDeflection);

// Saves the shape's current triangulation for the next open. Never throws.
void store(const TopoDS_Shape& shape, double deflection, double angularDeflection);

} // namespace meshcache
} // namespace materializr
