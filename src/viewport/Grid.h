#pragma once

#include "gl_common.h"

#include <glm/glm.hpp>

namespace materializr {

/// Renders an infinite grid on an arbitrary plane using a full-screen quad
/// shader. Defaults to the y=0 ground plane but can be retargeted to any plane
/// (e.g. the active sketch plane) so a from-scratch sketch on XY/XZ/YZ shows the
/// same grid face-on.
class Grid {
public:
    Grid();
    ~Grid();

    /// The plane the grid lies on: an origin, two orthonormal in-plane basis
    /// vectors (grid X/Y), and the plane normal. Defaults to the XZ ground.
    struct Plane {
        glm::vec3 origin{0.0f, 0.0f, 0.0f};
        glm::vec3 u{1.0f, 0.0f, 0.0f};
        glm::vec3 v{0.0f, 0.0f, 1.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
    };

    /// Initialize the grid (compile shaders, create VAO).
    /// Call once after OpenGL context is ready.
    bool initialize();

    /// Render the grid on `plane`. `minorStep` is the minor line spacing in world
    /// units (major lines every 10×). The grid fades with distance from
    /// `fadeCenter` over `fadeDistance` world units — distance-based so it stays
    /// visible under orthographic projection (unlike the old depth-based fade).
    void render(const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& fadeCenter, float fadeDistance,
                const Plane& plane, float minorStep);

private:
    bool compileShader(unsigned int& shader, unsigned int type, const char* source);
    bool linkProgram(unsigned int vertShader, unsigned int fragShader);

    unsigned int m_shaderProgram = 0;
    unsigned int m_vao = 0; // Dummy VAO for the full-screen quad trick

    // Uniform locations
    int m_locViewProjection = -1;
    int m_locInvViewProjection = -1;
    int m_locFadeCenter = -1;
    int m_locFadeDistance = -1;
    int m_locPlaneOrigin = -1;
    int m_locPlaneU = -1;
    int m_locPlaneV = -1;
    int m_locPlaneNormal = -1;
    int m_locScale = -1;
};

} // namespace materializr
