#pragma once
#include "gl_common.h"
#include <glm/glm.hpp>
#include <TopoDS_Shape.hxx>
#include <vector>

class SelectionManager;
class Document;

namespace materializr {

class SelectionHighlight {
public:
    SelectionHighlight();
    ~SelectionHighlight();

    bool initialize();

    void render(const SelectionManager& sel, const Document& doc,
                const glm::mat4& view, const glm::mat4& projection);

    // Width (in pixels) of highlighted edges and body outlines. Clamped to a
    // sane range; what the driver actually honours depends on its max line
    // width, but most support up to ~10.
    void setLineWidth(float w);

private:
    void renderFace(const TopoDS_Shape& face, const glm::mat4& vp, const glm::vec3& color);
    void renderEdge(const TopoDS_Shape& edge, const glm::mat4& vp, const glm::vec3& color);
    void renderBody(const TopoDS_Shape& body, const glm::mat4& vp, const glm::vec3& color);

    bool compileShader(unsigned int& shader, unsigned int type, const char* source);

    // Upload `verts` (xyz triplets, GL_LINES order) and draw them as quads of
    // `halfWidthPx` pixels using the geometry-shader line program. Used by both
    // edge and body highlighting so thickness is honoured in core-profile GL.
    void drawThickLines(const std::vector<float>& verts, const glm::mat4& vp,
                        const glm::vec3& color, float halfWidthPx);

    // Faces are a translucent triangle tint (no geometry shader).
    unsigned int m_program = 0;
    int m_locMVP = -1;
    int m_locColor = -1;

    // Lines: a separate program whose geometry shader expands each segment into
    // a screen-space quad, since glLineWidth > 1 is not honoured in core profile.
    unsigned int m_lineProgram = 0;
    int m_locLineMVP = -1;
    int m_locLineColor = -1;
    int m_locViewport = -1;
    int m_locHalfWidth = -1;

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;

    // Highlighted-edge line width in pixels. Body outlines render slightly
    // thinner so a whole selected body stays distinguishable from single edges.
    float m_edgeLineWidth = 3.0f;
};

} // namespace materializr
