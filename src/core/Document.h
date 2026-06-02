#pragma once
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <TopoDS_Shape.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

namespace materializr { class Sketch; class EventBus; }

struct BodyEntry {
    int id;
    std::string name;
    TopoDS_Shape shape;
    bool visible = true;
    glm::vec3 color = glm::vec3(0.80f, 0.80f, 0.82f); // default: light grey
    // -1 = at the root (not in any folder). >0 = a FolderEntry::id.
    int folderId = -1;
};

// Bodies can be grouped under a folder for organisation in the Items panel.
// Folder visibility and colour CASCADE to its bodies — toggling the folder
// hides/shows every body inside; setting the colour overwrites every body's
// colour (which can still be re-customised per body afterwards).
struct FolderEntry {
    int id;
    std::string name;
    bool visible = true;
    glm::vec3 color = glm::vec3(0.80f, 0.80f, 0.82f);
    bool expanded = true; // UI-only — collapsed folders hide children in panel
};

struct PlaneEntry {
    int id;
    std::string name;
    gp_Pln plane;
    bool visible = true;
    // Half-size of the rendered translucent quad in mm. Free to grow later
    // for autoscale; 50 mm (= 100 mm square) is a reasonable default that's
    // clearly visible against a typical part without dominating the scene.
    double halfSize = 50.0;
};

// Construction axis — a stored ray (origin + unit direction). Used as the
// rotation axis for Revolve (post-0.6) and any other "around a line"
// operation. Same plumbing shape as PlaneEntry: id / name / visibility /
// a render-extent (halfLength for the drawn segment in mm).
struct AxisEntry {
    int id;
    std::string name;
    gp_Pnt origin;
    gp_Dir direction;
    bool visible = true;
    double halfLength = 50.0; // visible segment is ±halfLength along direction
};

struct SketchEntry {
    int id;
    std::string name;
    std::shared_ptr<materializr::Sketch> sketch;
    bool visible = true;
};

class Document {
public:
    Document();
    ~Document();

    void setEventBus(materializr::EventBus* bus) { m_eventBus = bus; }

    // Body management
    int addBody(const TopoDS_Shape& shape, const std::string& name = "");
    // Create-or-reuse helper for undoable operations that recreate a body on
    // redo (Extrude / Pattern / Mirror / etc.). First call passes `id == -1`
    // and gets a fresh id allocated. Subsequent calls (redo after undo+remove)
    // pass the previously-assigned `id` and the body is reinstated under that
    // id, restoring folderId / colour / visibility / name from the tombstone
    // that removeBody stashed. `id` is updated in place to the final body id.
    void addOrPutBody(int& id, const TopoDS_Shape& shape, const std::string& name = "");
    void removeBody(int id);
    void updateBody(int id, const TopoDS_Shape& shape);
    // Add a body with an explicit id, or update the body that already has that
    // id. Keeps ids stable across save/load and history replay; bumps the id
    // counter so later auto-assigned ids don't collide.
    void putBody(int id, const TopoDS_Shape& shape, const std::string& name = "");
    const TopoDS_Shape& getBody(int id) const;
    std::string getBodyName(int id) const;
    void setBodyName(int id, const std::string& name);
    void setBodyVisible(int id, bool visible);
    bool isBodyVisible(int id) const;
    glm::vec3 getBodyColor(int id) const;
    void setBodyColor(int id, const glm::vec3& color);
    std::vector<int> getAllBodyIds() const;

    // Folder management. Folders are pure UI grouping over bodies — they
    // don't own bodies (a body keeps its id and is only assigned a folderId).
    int addFolder(const std::string& name = "");
    void removeFolder(int folderId); // bodies in it return to root (folderId=-1)
    std::vector<int> getAllFolderIds() const;
    std::string getFolderName(int folderId) const;
    void setFolderName(int folderId, const std::string& name);
    bool isFolderVisible(int folderId) const;
    // Setting folder visibility CASCADES to every member body's visibility.
    void setFolderVisible(int folderId, bool visible);
    glm::vec3 getFolderColor(int folderId) const;
    // Setting folder colour CASCADES to every member body's colour.
    void setFolderColor(int folderId, const glm::vec3& color);
    bool isFolderExpanded(int folderId) const;
    void setFolderExpanded(int folderId, bool expanded);
    // Bodies-by-folder lookups.
    int getBodyFolder(int bodyId) const; // -1 if at root
    void setBodyFolder(int bodyId, int folderId); // -1 = move to root
    std::vector<int> getBodiesInFolder(int folderId) const; // folderId=-1 = root bodies

    // Sketch management
    int addSketch(std::shared_ptr<materializr::Sketch> sketch, const std::string& name = "");
    void removeSketch(int id);
    std::shared_ptr<materializr::Sketch> getSketch(int id) const;
    std::string getSketchName(int id) const;
    void setSketchName(int id, const std::string& name);
    void setSketchVisible(int id, bool visible);
    bool isSketchVisible(int id) const;
    std::vector<int> getAllSketchIds() const;
    int sketchCount() const;
    // Reverse lookup: returns the document id of the given Sketch* (compared
    // by raw pointer against the held shared_ptrs), or -1 if not found.
    // SketchEditOp::serializeWithDocument uses this to stamp the live id
    // into the serialized snapshot.
    int findSketchId(const materializr::Sketch* sk) const;

    // Construction planes — first-class document objects parallel to sketches.
    // PlaneAddedEvent / PlaneRemovedEvent let the renderer + Items panel
    // react without polling each frame.
    int addPlane(const gp_Pln& plane, const std::string& name = "");
    void removePlane(int id);
    // Update an existing plane's gp_Pln (move + rotate gizmo write-back).
    // Fires PlaneChangedEvent so the renderer / selection-aware UI updates.
    void setPlane(int id, const gp_Pln& plane);
    const PlaneEntry* getPlane(int id) const;
    std::string getPlaneName(int id) const;
    void setPlaneName(int id, const std::string& name);
    void setPlaneVisible(int id, bool visible);
    bool isPlaneVisible(int id) const;
    std::vector<int> getAllPlaneIds() const;
    int planeCount() const;

    // Construction axes — same shape as construction planes. Used by
    // Revolve and any other op that needs to rotate around a line.
    // Axis* events let the renderer + Items panel react without polling.
    int addAxis(const gp_Pnt& origin, const gp_Dir& direction,
                const std::string& name = "");
    void removeAxis(int id);
    void setAxis(int id, const gp_Pnt& origin, const gp_Dir& direction);
    const AxisEntry* getAxis(int id) const;
    std::string getAxisName(int id) const;
    void setAxisName(int id, const std::string& name);
    void setAxisVisible(int id, bool visible);
    bool isAxisVisible(int id) const;
    std::vector<int> getAllAxisIds() const;
    int axisCount() const;

    // Clear everything
    void clear();

    // Body count
    int bodyCount() const;

private:
    int findBodyIndex(int id) const;
    int findSketchIndex(int id) const;
    int findFolderIndex(int id) const;

    std::vector<BodyEntry> m_bodies;
    std::vector<PlaneEntry> m_planes;
    std::vector<AxisEntry> m_axes;
    std::vector<SketchEntry> m_sketches;
    std::vector<FolderEntry> m_folders;
    // Tombstones: when a body is removed, its non-geometry metadata (folderId,
    // colour, visibility, name) is stashed here keyed by id. When putBody is
    // later called with the same id (the typical redo-after-undo path through
    // ops like Extrude / Pattern / Mirror), the metadata is restored. Without
    // this, a body recreated after undo would silently snap back to the root
    // folder, default colour, and visible=true.
    std::map<int, BodyEntry> m_bodyTombstones;
    int m_nextBodyId = 1;
    int m_nextPlaneId = 1;
    int m_nextAxisId = 1;
    int m_nextSketchId = 1;
    int m_nextFolderId = 1;
    materializr::EventBus* m_eventBus = nullptr;
};
