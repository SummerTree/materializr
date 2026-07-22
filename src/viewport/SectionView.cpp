#include "gl_common.h"
#include "SectionView.h"
#include "SectionCap.h"
#include "../core/Document.h"

#include <glm/gtc/type_ptr.hpp>

#include <BRepAlgoAPI_Section.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <BRep_Tool.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax3.hxx>

#include <BRepBuilderAPI_MakeFace.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressRange.hxx>
#include <Message_ProgressScope.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Face.hxx>

#include <cstdio>

namespace materializr {

namespace {
class SectionCancelBreak : public Message_ProgressIndicator {
public:
    DEFINE_STANDARD_RTTI_INLINE(SectionCancelBreak, Message_ProgressIndicator)
    explicit SectionCancelBreak(const std::atomic<bool>* f) : m_f(f) {}
    Standard_Boolean UserBreak() override { return m_f && m_f->load(); }
protected:
    void Show(const Message_ProgressScope&, const Standard_Boolean) override {}
private:
    const std::atomic<bool>* m_f;
};
} // namespace

static const char* s_sectionVertSource = R"(
#version 330 core
layout(location = 0) in vec3 a_position;
uniform mat4 u_mvp;
void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";

static const char* s_sectionFragSource = R"(
#version 330 core
uniform vec3 u_color;
out vec4 fragColor;
void main() {
    fragColor = vec4(u_color, 1.0);
}
)";

// Cap fill: the cross-section is planar (one normal for the whole cap), so a
// flat shade off that normal is enough to read as solid cut material.
static const char* s_capVertSource = R"(
#version 330 core
layout(location = 0) in vec3 a_position;
uniform mat4 u_mvp;
void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";

static const char* s_capFragSource = R"(
#version 330 core
uniform vec3 u_color;
uniform vec3 u_normal; // cut-plane normal (world)
out vec4 fragColor;
void main() {
    vec3 L = normalize(vec3(0.4, 0.7, 0.6));
    float d = 0.45 + 0.55 * abs(dot(normalize(u_normal), L));
    fragColor = vec4(u_color * d, 1.0);
}
)";

SectionView::SectionView() {}

SectionView::~SectionView() {
    if (m_program) glDeleteProgram(m_program);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_capProgram) glDeleteProgram(m_capProgram);
    if (m_capVao) glDeleteVertexArrays(1, &m_capVao);
    if (m_capVbo) glDeleteBuffers(1, &m_capVbo);
}

bool SectionView::initialize() {
    unsigned int vert = 0, frag = 0;
    if (!compileShader(vert, GL_VERTEX_SHADER, s_sectionVertSource)) return false;
    if (!compileShader(frag, GL_FRAGMENT_SHADER, s_sectionFragSource)) {
        glDeleteShader(vert);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vert);
    glAttachShader(m_program, frag);
    glLinkProgram(m_program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    int success = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(m_program, 512, nullptr, log);
        std::fprintf(stderr, "SectionView link error: %s\n", log);
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    m_locMVP = glGetUniformLocation(m_program, "u_mvp");
    m_locColor = glGetUniformLocation(m_program, "u_color");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    // Cap fill program
    unsigned int cvert = 0, cfrag = 0;
    if (!compileShader(cvert, GL_VERTEX_SHADER, s_capVertSource)) return false;
    if (!compileShader(cfrag, GL_FRAGMENT_SHADER, s_capFragSource)) {
        glDeleteShader(cvert);
        return false;
    }
    m_capProgram = glCreateProgram();
    glAttachShader(m_capProgram, cvert);
    glAttachShader(m_capProgram, cfrag);
    glLinkProgram(m_capProgram);
    glDeleteShader(cvert);
    glDeleteShader(cfrag);

    int capOk = 0;
    glGetProgramiv(m_capProgram, GL_LINK_STATUS, &capOk);
    if (!capOk) {
        char log[512];
        glGetProgramInfoLog(m_capProgram, 512, nullptr, log);
        std::fprintf(stderr, "SectionView cap link error: %s\n", log);
        glDeleteProgram(m_capProgram);
        m_capProgram = 0;
        return false;
    }

    m_capLocMVP = glGetUniformLocation(m_capProgram, "u_mvp");
    m_capLocColor = glGetUniformLocation(m_capProgram, "u_color");
    m_capLocNormal = glGetUniformLocation(m_capProgram, "u_normal");

    glGenVertexArrays(1, &m_capVao);
    glGenBuffers(1, &m_capVbo);

    return true;
}

void SectionView::setDocument(const Document* doc) {
    m_document = doc;
}

void SectionView::setPlane(const gp_Pln& plane) {
    m_plane = plane;
}

void SectionView::setOffset(float offset) {
    m_offset = offset;
}

void SectionView::setEnabled(bool enabled) {
    m_enabled = enabled;
}

bool SectionView::isEnabled() const {
    return m_enabled;
}

SectionView::Result SectionView::compute(
    const std::vector<std::pair<TopoDS_Shape, glm::vec3>>& bodies,
    const gp_Pln& cuttingPlane, const std::atomic<bool>* cancel) {
    Result out;
    {
        gp_Dir cn = cuttingPlane.Axis().Direction();
        out.capNormal = glm::vec3(static_cast<float>(cn.X()),
                                  static_cast<float>(cn.Y()),
                                  static_cast<float>(cn.Z()));
    }
    for (const auto& [shape, color] : bodies) {
        if (cancel && cancel->load()) return out;
        if (shape.IsNull()) continue;
        try {
            // BRepAlgoAPI_Section derives from the boolean machinery, so the
            // cancel token can abort MID-boolean via OCCT user-break — a
            // superseded/disabled 100-second section dies quickly instead of
            // finishing into the void.
            BRepAlgoAPI_Section section;
            TopTools_ListOfShape args, tools;
            args.Append(shape);
            TopoDS_Face planeFace =
                BRepBuilderAPI_MakeFace(cuttingPlane).Face();
            tools.Append(planeFace);
            section.SetArguments(args);
            section.SetTools(tools);
            section.SetRunParallel(Standard_True);
            if (cancel) {
                Handle(SectionCancelBreak) brk =
                    new SectionCancelBreak(cancel);
                section.Build(brk->Start());
            } else {
                section.Build();
            }
            if (!section.IsDone()) continue;

            const TopoDS_Shape& result = section.Shape();

            // Discretize each edge in the section result
            for (TopExp_Explorer exp(result, TopAbs_EDGE); exp.More(); exp.Next()) {
                const TopoDS_Edge& edge = TopoDS::Edge(exp.Current());
                if (BRep_Tool::Degenerated(edge)) continue;

                try {
                    BRepAdaptor_Curve curve(edge);
                    GCPnts_TangentialDeflection discretizer(curve, 0.1, 0.1);

                    int nbPoints = discretizer.NbPoints();
                    if (nbPoints < 2) continue;

                    for (int i = 1; i < nbPoints; ++i) {
                        gp_Pnt p1 = discretizer.Value(i);
                        gp_Pnt p2 = discretizer.Value(i + 1);

                        SectionLine line;
                        line.start = glm::vec3(
                            static_cast<float>(p1.X()),
                            static_cast<float>(p1.Y()),
                            static_cast<float>(p1.Z()));
                        line.end = glm::vec3(
                            static_cast<float>(p2.X()),
                            static_cast<float>(p2.Y()),
                            static_cast<float>(p2.Z()));
                        out.lines.push_back(line);
                    }
                } catch (...) {
                    continue;
                }
            }

            if (cancel && cancel->load()) return out;
            // --- Cross-section cap ---
            // Without a filled cap the clipped solid reads as a hollow shell.
            CapMesh cap;
            cap.color = color;
            if (computeSectionCap(shape, cuttingPlane, cap.positions))
                out.caps.push_back(std::move(cap));
        } catch (...) {
            continue;
        }
    }
    return out;
}

void SectionView::apply(Result&& r) {
    m_lines = std::move(r.lines);
    m_caps = std::move(r.caps);
    m_capNormal = r.capNormal;
}

void SectionView::update() {
    m_lines.clear();
    m_caps.clear();

    if (!m_enabled || !m_document) return;

    // Build the actual cutting plane with offset applied along the normal
    gp_Pln cuttingPlane = m_plane;
    if (m_offset != 0.0f) {
        gp_Pnt origin = cuttingPlane.Location();
        gp_Dir normal = cuttingPlane.Axis().Direction();
        origin.Translate(gp_Vec(normal) * static_cast<double>(m_offset));
        cuttingPlane.SetLocation(origin);
    }

    std::vector<std::pair<TopoDS_Shape, glm::vec3>> bodies;
    for (int id : m_document->getAllBodyIds()) {
        if (!m_document->isBodyVisible(id)) continue;
        try {
            const TopoDS_Shape& shape = m_document->getBody(id);
            if (shape.IsNull()) continue;
            bodies.emplace_back(shape, m_document->getBodyColor(id));
        } catch (...) { continue; }
    }
    apply(compute(bodies, cuttingPlane, nullptr));
}

void SectionView::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!m_enabled) return;

    glm::mat4 mvpCap = projection * view;

    // --- Filled caps first (depth-tested so walls in front still occlude) ---
    if (m_capProgram && !m_caps.empty()) {
        glUseProgram(m_capProgram);
        glUniformMatrix4fv(m_capLocMVP, 1, GL_FALSE, glm::value_ptr(mvpCap));
        glUniform3fv(m_capLocNormal, 1, glm::value_ptr(m_capNormal));

        const GLboolean cullWas = glIsEnabled(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE); // cap winding is not guaranteed toward camera

        glBindVertexArray(m_capVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_capVbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        for (const auto& cap : m_caps) {
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(cap.positions.size() * sizeof(float)),
                         cap.positions.data(), GL_DYNAMIC_DRAW);
            glUniform3fv(m_capLocColor, 1, glm::value_ptr(cap.color));
            glDrawArrays(GL_TRIANGLES, 0,
                         static_cast<GLsizei>(cap.positions.size() / 3));
        }
        glBindVertexArray(0);
        if (cullWas) glEnable(GL_CULL_FACE); // restore; do not leak state
    }

    if (m_lines.empty() || !m_program) {
        glUseProgram(0);
        return;
    }

    // Upload line data to VBO
    std::vector<float> vertices;
    vertices.reserve(m_lines.size() * 6);
    for (const auto& line : m_lines) {
        vertices.push_back(line.start.x);
        vertices.push_back(line.start.y);
        vertices.push_back(line.start.z);
        vertices.push_back(line.end.x);
        vertices.push_back(line.end.y);
        vertices.push_back(line.end.z);
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glm::mat4 mvp = projection * view;

    glUseProgram(m_program);
    glUniformMatrix4fv(m_locMVP, 1, GL_FALSE, glm::value_ptr(mvp));

    // Orange/red section line color
    glm::vec3 sectionColor(0.95f, 0.4f, 0.1f);
    glUniform3fv(m_locColor, 1, glm::value_ptr(sectionColor));

    // Disable depth test so section lines are always visible
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);

    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size() / 3));

    glEnable(GL_DEPTH_TEST);
    glLineWidth(1.0f);

    glBindVertexArray(0);
    glUseProgram(0);
}

bool SectionView::compileShader(unsigned int& shader, unsigned int type, const char* source) {
    shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::fprintf(stderr, "SectionView shader error: %s\n", log);
        glDeleteShader(shader);
        shader = 0;
        return false;
    }
    return true;
}

} // namespace materializr
