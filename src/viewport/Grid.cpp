#include "gl_common.h"

#include "Grid.h"

#include <glm/gtc/type_ptr.hpp>
#include <cstdio>

namespace materializr {

// Embedded grid shader sources (matching shaders/grid.vert and shaders/grid.frag)
static const char* s_gridVertSource = R"(
#version 330 core

uniform mat4 u_viewProjection;
uniform mat4 u_invViewProjection;

out vec3 v_nearPoint;
out vec3 v_farPoint;

vec3 gridPlane[6] = vec3[](
    vec3( 1,  1, 0), vec3(-1, -1, 0), vec3(-1,  1, 0),
    vec3(-1, -1, 0), vec3( 1,  1, 0), vec3( 1, -1, 0)
);

vec3 unprojectPoint(float x, float y, float z) {
    vec4 unprojected = u_invViewProjection * vec4(x, y, z, 1.0);
    return unprojected.xyz / unprojected.w;
}

void main() {
    vec3 p = gridPlane[gl_VertexID];
    // Use the true near plane (NDC z = -1), not z = 0. Under orthographic
    // projection depth is linear, so z = 0 is the mid-depth point, which can sit
    // behind the target plane and make the ray parameter negative — discarding
    // the whole grid. The near plane is always in front of the camera.
    v_nearPoint = unprojectPoint(p.x, p.y, -1.0);
    v_farPoint  = unprojectPoint(p.x, p.y,  1.0);
    gl_Position = vec4(p, 1.0);
}
)";

static const char* s_gridFragSource = R"(
#version 330 core

in vec3 v_nearPoint;
in vec3 v_farPoint;

uniform mat4 u_viewProjection;
uniform vec3 u_fadeCenter;     // grid fades out around this world point
uniform float u_fadeDistance;  // world-space fade radius
uniform vec3 u_planeOrigin;
uniform vec3 u_planeU;         // in-plane basis (grid X), unit length
uniform vec3 u_planeV;         // in-plane basis (grid Y), unit length
uniform vec3 u_planeNormal;
uniform float u_scale;         // lines every 1/u_scale plane units (minor)

out vec4 fragColor;

float computeDepth(vec3 pos) {
    vec4 clipPos = u_viewProjection * vec4(pos, 1.0);
    return (clipPos.z / clipPos.w) * 0.5 + 0.5;
}

// `uv` are the fragment's in-plane coordinates (already in plane units).
vec4 grid(vec2 uv, float scale, vec4 lineColor) {
    vec2 coord = uv * scale;
    vec2 derivative = fwidth(coord);
    vec2 g = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line = min(g.x, g.y);
    float minU = min(derivative.x, 1.0);
    float minV = min(derivative.y, 1.0);

    vec4 color = lineColor;
    color.a = 1.0 - min(line, 1.0);

    // U-axis highlight (red) where the V coordinate is ~0
    if (coord.y > -0.5 * minV && coord.y < 0.5 * minV) color = vec4(0.8, 0.2, 0.2, 1.0);
    // V-axis highlight (blue) where the U coordinate is ~0
    if (coord.x > -0.5 * minU && coord.x < 0.5 * minU) color = vec4(0.2, 0.2, 0.8, 1.0);

    return color;
}

void main() {
    vec3 dir = v_farPoint - v_nearPoint;
    float denom = dot(dir, u_planeNormal);
    if (abs(denom) < 1e-6) discard;
    float t = dot(u_planeOrigin - v_nearPoint, u_planeNormal) / denom;
    if (t < 0.0) discard; // plane behind the camera

    vec3 fragPos3D = v_nearPoint + t * dir;
    gl_FragDepth = computeDepth(fragPos3D);

    // In-plane coordinates relative to the plane origin.
    vec3 rel = fragPos3D - u_planeOrigin;
    vec2 uv = vec2(dot(rel, u_planeU), dot(rel, u_planeV));

    // Distance-based fade (works in both perspective and orthographic).
    float dist = length(fragPos3D - u_fadeCenter);
    float fade = clamp(1.0 - dist / max(u_fadeDistance, 1e-3), 0.0, 1.0);

    // Minor lines every 1/u_scale units; major lines every 10× brighter so the
    // every-10th line reads clearly. Major also gets an alpha boost so it stays
    // prominent where it coincides with a minor line.
    vec4 minorColor = grid(uv, u_scale, vec4(0.34, 0.34, 0.38, 1.0));
    vec4 majorColor = grid(uv, u_scale * 0.1, vec4(0.85, 0.87, 0.95, 1.0));

    vec4 color = minorColor;
    if (majorColor.a > 0.0) {
        // Blend toward the brighter major line by its coverage so it dominates.
        color.rgb = mix(minorColor.rgb, majorColor.rgb, majorColor.a);
        color.a = max(minorColor.a, majorColor.a);
    }

    color.a *= fade;
    if (color.a < 0.001) discard;

    fragColor = color;
}
)";

Grid::Grid() {}

Grid::~Grid()
{
    if (m_shaderProgram) {
        glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
}

bool Grid::initialize()
{
    // Compile shaders
    unsigned int vertShader = 0, fragShader = 0;
    if (!compileShader(vertShader, GL_VERTEX_SHADER, s_gridVertSource)) {
        return false;
    }
    if (!compileShader(fragShader, GL_FRAGMENT_SHADER, s_gridFragSource)) {
        glDeleteShader(vertShader);
        return false;
    }
    if (!linkProgram(vertShader, fragShader)) {
        glDeleteShader(vertShader);
        glDeleteShader(fragShader);
        return false;
    }
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    // Cache uniform locations
    m_locViewProjection = glGetUniformLocation(m_shaderProgram, "u_viewProjection");
    m_locInvViewProjection = glGetUniformLocation(m_shaderProgram, "u_invViewProjection");
    m_locFadeCenter = glGetUniformLocation(m_shaderProgram, "u_fadeCenter");
    m_locFadeDistance = glGetUniformLocation(m_shaderProgram, "u_fadeDistance");
    m_locPlaneOrigin = glGetUniformLocation(m_shaderProgram, "u_planeOrigin");
    m_locPlaneU = glGetUniformLocation(m_shaderProgram, "u_planeU");
    m_locPlaneV = glGetUniformLocation(m_shaderProgram, "u_planeV");
    m_locPlaneNormal = glGetUniformLocation(m_shaderProgram, "u_planeNormal");
    m_locScale = glGetUniformLocation(m_shaderProgram, "u_scale");

    // Create a dummy VAO (required for core profile, even with no vertex attributes)
    glGenVertexArrays(1, &m_vao);

    return true;
}

void Grid::render(const glm::mat4& view, const glm::mat4& projection,
                  const glm::vec3& fadeCenter, float fadeDistance,
                  const Plane& plane, float minorStep)
{
    if (!m_shaderProgram) return;

    glm::mat4 vp = projection * view;
    glm::mat4 invVP = glm::inverse(vp);
    float scale = (minorStep > 1e-4f) ? (1.0f / minorStep) : 1.0f;

    glUseProgram(m_shaderProgram);
    glUniformMatrix4fv(m_locViewProjection, 1, GL_FALSE, glm::value_ptr(vp));
    glUniformMatrix4fv(m_locInvViewProjection, 1, GL_FALSE, glm::value_ptr(invVP));
    glUniform3fv(m_locFadeCenter, 1, glm::value_ptr(fadeCenter));
    glUniform1f(m_locFadeDistance, fadeDistance);
    glUniform3fv(m_locPlaneOrigin, 1, glm::value_ptr(plane.origin));
    glUniform3fv(m_locPlaneU, 1, glm::value_ptr(plane.u));
    glUniform3fv(m_locPlaneV, 1, glm::value_ptr(plane.v));
    glUniform3fv(m_locPlaneNormal, 1, glm::value_ptr(plane.normal));
    glUniform1f(m_locScale, scale);

    // Enable blending for grid transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw the full-screen quad (6 vertices from gl_VertexID)
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glUseProgram(0);
}

bool Grid::compileShader(unsigned int& shader, unsigned int type, const char* source)
{
    shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::fprintf(stderr, "Grid shader compilation failed: %s\n", infoLog);
        glDeleteShader(shader);
        shader = 0;
        return false;
    }
    return true;
}

bool Grid::linkProgram(unsigned int vertShader, unsigned int fragShader)
{
    m_shaderProgram = glCreateProgram();
    glAttachShader(m_shaderProgram, vertShader);
    glAttachShader(m_shaderProgram, fragShader);
    glLinkProgram(m_shaderProgram);

    int success = 0;
    glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(m_shaderProgram, 512, nullptr, infoLog);
        std::fprintf(stderr, "Grid shader linking failed: %s\n", infoLog);
        glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
        return false;
    }
    return true;
}

} // namespace materializr
