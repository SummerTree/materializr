#include "Document.h"
#include "EventBus.h"
#include "Events.h"
#include "../modeling/Sketch.h"
#include <algorithm>
#include <stdexcept>

Document::Document() = default;
Document::~Document() = default;

int Document::addBody(const TopoDS_Shape& shape, const std::string& name) {
    BodyEntry entry;
    entry.id = m_nextBodyId++;
    entry.name = name.empty() ? ("Body " + std::to_string(entry.id)) : name;
    entry.shape = shape;
    entry.visible = true;
    m_bodies.push_back(std::move(entry));
    if (m_eventBus) m_eventBus->publish(materializr::DocumentModifiedEvent{true});
    return m_bodies.back().id;
}

void Document::addOrPutBody(int& id, const TopoDS_Shape& shape, const std::string& name) {
    if (id < 0) {
        id = addBody(shape, name);
    } else {
        // Reuse the prior id. putBody picks up the tombstone-stashed metadata
        // from removeBody, so folderId / colour / visibility / name persist
        // across undo/redo cycles.
        putBody(id, shape, name);
    }
}

void Document::removeBody(int id) {
    int idx = findBodyIndex(id);
    if (idx >= 0) {
        // Stash metadata before erasing so a later putBody with the same id
        // (undo/redo path through Extrude / Pattern / Mirror / etc.) can
        // restore the body's folderId, colour, visibility, and name.
        m_bodyTombstones[id] = m_bodies[idx];
        m_bodies.erase(m_bodies.begin() + idx);
        if (m_eventBus) m_eventBus->publish(materializr::DocumentModifiedEvent{true});
    }
}

void Document::updateBody(int id, const TopoDS_Shape& shape) {
    int idx = findBodyIndex(id);
    if (idx >= 0) {
        m_bodies[idx].shape = shape;
        if (m_eventBus) m_eventBus->publish(materializr::DocumentModifiedEvent{true});
    }
}

void Document::putBody(int id, const TopoDS_Shape& shape, const std::string& name) {
    int idx = findBodyIndex(id);
    if (idx >= 0) {
        m_bodies[idx].shape = shape;
        if (!name.empty()) m_bodies[idx].name = name;
    } else {
        // If this id was previously removed (typical undo/redo through an op
        // that recreates a body), pull its metadata back from the tombstone
        // so folderId / colour / visibility / name aren't silently lost.
        auto tomb = m_bodyTombstones.find(id);
        if (tomb != m_bodyTombstones.end()) {
            BodyEntry entry = tomb->second;
            entry.id = id;
            entry.shape = shape;
            if (!name.empty()) entry.name = name;
            m_bodies.push_back(std::move(entry));
            m_bodyTombstones.erase(tomb);
        } else {
            BodyEntry entry;
            entry.id = id;
            entry.name = name.empty() ? ("Body " + std::to_string(id)) : name;
            entry.shape = shape;
            entry.visible = true;
            m_bodies.push_back(std::move(entry));
        }
    }
    if (id >= m_nextBodyId) m_nextBodyId = id + 1;
    if (m_eventBus) m_eventBus->publish(materializr::DocumentModifiedEvent{true});
}

const TopoDS_Shape& Document::getBody(int id) const {
    int idx = findBodyIndex(id);
    if (idx < 0) {
        throw std::runtime_error("Body not found: " + std::to_string(id));
    }
    return m_bodies[idx].shape;
}

std::string Document::getBodyName(int id) const {
    int idx = findBodyIndex(id);
    if (idx < 0) {
        return "";
    }
    return m_bodies[idx].name;
}

void Document::setBodyName(int id, const std::string& name) {
    int idx = findBodyIndex(id);
    if (idx >= 0) {
        m_bodies[idx].name = name;
    }
}

void Document::setBodyVisible(int id, bool visible) {
    int idx = findBodyIndex(id);
    if (idx >= 0) {
        m_bodies[idx].visible = visible;
    }
}

bool Document::isBodyVisible(int id) const {
    int idx = findBodyIndex(id);
    if (idx < 0) {
        return false;
    }
    return m_bodies[idx].visible;
}

glm::vec3 Document::getBodyColor(int id) const {
    int idx = findBodyIndex(id);
    if (idx < 0) {
        return glm::vec3(0.80f, 0.80f, 0.82f);
    }
    return m_bodies[idx].color;
}

void Document::setBodyColor(int id, const glm::vec3& color) {
    int idx = findBodyIndex(id);
    if (idx >= 0) {
        m_bodies[idx].color = color;
    }
}

std::vector<int> Document::getAllBodyIds() const {
    std::vector<int> ids;
    ids.reserve(m_bodies.size());
    for (const auto& body : m_bodies) {
        ids.push_back(body.id);
    }
    return ids;
}

int Document::addSketch(std::shared_ptr<materializr::Sketch> sketch, const std::string& name) {
    SketchEntry entry;
    entry.id = m_nextSketchId++;
    entry.name = name.empty() ? ("Sketch " + std::to_string(entry.id)) : name;
    entry.sketch = std::move(sketch);
    entry.visible = true;
    m_sketches.push_back(std::move(entry));
    return m_sketches.back().id;
}

void Document::removeSketch(int id) {
    int idx = findSketchIndex(id);
    if (idx >= 0) {
        m_sketches.erase(m_sketches.begin() + idx);
    }
}

std::shared_ptr<materializr::Sketch> Document::getSketch(int id) const {
    int idx = findSketchIndex(id);
    if (idx < 0) return nullptr;
    return m_sketches[idx].sketch;
}

std::string Document::getSketchName(int id) const {
    int idx = findSketchIndex(id);
    if (idx < 0) return "";
    return m_sketches[idx].name;
}

void Document::setSketchName(int id, const std::string& name) {
    int idx = findSketchIndex(id);
    if (idx >= 0) m_sketches[idx].name = name;
}

void Document::setSketchVisible(int id, bool visible) {
    int idx = findSketchIndex(id);
    if (idx >= 0) m_sketches[idx].visible = visible;
}

bool Document::isSketchVisible(int id) const {
    int idx = findSketchIndex(id);
    if (idx < 0) return false;
    return m_sketches[idx].visible;
}

std::vector<int> Document::getAllSketchIds() const {
    std::vector<int> ids;
    ids.reserve(m_sketches.size());
    for (const auto& s : m_sketches) ids.push_back(s.id);
    return ids;
}

int Document::sketchCount() const {
    return static_cast<int>(m_sketches.size());
}

int Document::findSketchIndex(int id) const {
    for (int i = 0; i < static_cast<int>(m_sketches.size()); ++i) {
        if (m_sketches[i].id == id) return i;
    }
    return -1;
}

int Document::addPlane(const gp_Pln& plane, const std::string& name) {
    PlaneEntry entry;
    entry.id = m_nextPlaneId++;
    entry.name = name.empty() ? ("Plane " + std::to_string(entry.id)) : name;
    entry.plane = plane;
    m_planes.push_back(std::move(entry));
    return m_planes.back().id;
}

void Document::clear() {
    m_bodies.clear();
    m_planes.clear();
    m_sketches.clear();
    m_folders.clear();
    m_bodyTombstones.clear();
    m_nextBodyId = 1;
    m_nextPlaneId = 1;
    m_nextSketchId = 1;
    m_nextFolderId = 1;
}

// ---- Folders ---------------------------------------------------------------

int Document::addFolder(const std::string& name) {
    FolderEntry entry;
    entry.id = m_nextFolderId++;
    entry.name = name.empty() ? ("Folder " + std::to_string(entry.id)) : name;
    m_folders.push_back(std::move(entry));
    if (m_eventBus) m_eventBus->publish(materializr::DocumentModifiedEvent{true});
    return m_folders.back().id;
}

void Document::removeFolder(int folderId) {
    int idx = findFolderIndex(folderId);
    if (idx < 0) return;
    // Orphan members back to root, then erase.
    for (auto& b : m_bodies) {
        if (b.folderId == folderId) b.folderId = -1;
    }
    m_folders.erase(m_folders.begin() + idx);
    if (m_eventBus) m_eventBus->publish(materializr::DocumentModifiedEvent{true});
}

std::vector<int> Document::getAllFolderIds() const {
    std::vector<int> ids;
    ids.reserve(m_folders.size());
    for (const auto& f : m_folders) ids.push_back(f.id);
    return ids;
}

std::string Document::getFolderName(int folderId) const {
    int idx = findFolderIndex(folderId);
    return idx < 0 ? "" : m_folders[idx].name;
}

void Document::setFolderName(int folderId, const std::string& name) {
    int idx = findFolderIndex(folderId);
    if (idx >= 0) m_folders[idx].name = name;
}

bool Document::isFolderVisible(int folderId) const {
    int idx = findFolderIndex(folderId);
    return idx < 0 ? false : m_folders[idx].visible;
}

void Document::setFolderVisible(int folderId, bool visible) {
    int idx = findFolderIndex(folderId);
    if (idx < 0) return;
    m_folders[idx].visible = visible;
    // Cascade to members.
    for (auto& b : m_bodies) {
        if (b.folderId == folderId) b.visible = visible;
    }
}

glm::vec3 Document::getFolderColor(int folderId) const {
    int idx = findFolderIndex(folderId);
    return idx < 0 ? glm::vec3(0.80f, 0.80f, 0.82f) : m_folders[idx].color;
}

void Document::setFolderColor(int folderId, const glm::vec3& color) {
    int idx = findFolderIndex(folderId);
    if (idx < 0) return;
    m_folders[idx].color = color;
    // Cascade to members — overwrites their colour. Re-customisable per body
    // afterwards (per-body picker still works as before).
    for (auto& b : m_bodies) {
        if (b.folderId == folderId) b.color = color;
    }
}

bool Document::isFolderExpanded(int folderId) const {
    int idx = findFolderIndex(folderId);
    return idx < 0 ? false : m_folders[idx].expanded;
}

void Document::setFolderExpanded(int folderId, bool expanded) {
    int idx = findFolderIndex(folderId);
    if (idx >= 0) m_folders[idx].expanded = expanded;
}

int Document::getBodyFolder(int bodyId) const {
    int idx = findBodyIndex(bodyId);
    return idx < 0 ? -1 : m_bodies[idx].folderId;
}

void Document::setBodyFolder(int bodyId, int folderId) {
    int bidx = findBodyIndex(bodyId);
    if (bidx < 0) return;
    if (folderId >= 0 && findFolderIndex(folderId) < 0) return; // unknown folder
    m_bodies[bidx].folderId = folderId;
}

std::vector<int> Document::getBodiesInFolder(int folderId) const {
    std::vector<int> ids;
    for (const auto& b : m_bodies) {
        if (b.folderId == folderId) ids.push_back(b.id);
    }
    return ids;
}

int Document::findFolderIndex(int id) const {
    for (int i = 0; i < static_cast<int>(m_folders.size()); ++i) {
        if (m_folders[i].id == id) return i;
    }
    return -1;
}

int Document::bodyCount() const {
    return static_cast<int>(m_bodies.size());
}

int Document::findBodyIndex(int id) const {
    for (int i = 0; i < static_cast<int>(m_bodies.size()); ++i) {
        if (m_bodies[i].id == id) {
            return i;
        }
    }
    return -1;
}
