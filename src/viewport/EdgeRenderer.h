#pragma once
#include "gl_common.h"
#include <glm/glm.hpp>
#include <TopoDS_Shape.hxx>
#include <map>
#include <vector>

namespace materializr {

class EdgeRenderer {
public:
    EdgeRenderer();
    ~EdgeRenderer();

    /// Initialize shaders. Call once after OpenGL context is ready.
    bool initialize();

    /// Extract and buffer edges from a shape.
    /// Returns the mesh index for later reference.
    int addShape(const TopoDS_Shape& shape, float deflection = 0.1f);

    /// Per-body upsert. Re-extracts edges and binds the resulting mesh to
    /// `bodyId`, replacing the previous edges for that body in place. Lets
    /// callers update one body's edges during a push/pull preview without
    /// touching the rest.
    void setBodyEdges(int bodyId, const TopoDS_Shape& shape,
                      float deflection = 0.1f);

    /// Remove the edge mesh for `bodyId`. Slot stays in the array (vacant)
    /// to keep external indices stable; render() skips it.
    void removeBody(int bodyId);

    /// Render all edge meshes as dark lines.
    void render(const glm::mat4& view, const glm::mat4& projection);

    /// Remove all edge meshes.
    void clear();

    /// Slot index of `bodyId`'s edge mesh, or -1 if not present. Lets
    /// callers pair the edges' per-body model matrix with the shape
    /// renderer's during interactive previews.
    int findSlotByBody(int bodyId) const;

    /// Per-body model matrix — applied during render() so a live preview
    /// can transform the edges visually without re-extracting them. Cheap
    /// because the cached vertex data is unchanged; only the MVP uniform
    /// shifts per draw.
    void setModelMatrix(int slot, const glm::mat4& model);

private:
    struct EdgeMesh {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        int vertexCount = 0;
        int bodyId = -1;
        glm::mat4 modelMatrix = glm::mat4(1.0f);
    };

    bool compileShader(unsigned int& shader, unsigned int type, const char* source);

    std::vector<EdgeMesh> m_meshes;
    std::map<int, int> m_bodyToSlot;
    unsigned int m_program = 0;
    int m_locMVP = -1;
    int m_locColor = -1;
};

} // namespace materializr
