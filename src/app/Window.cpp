#include "app/Window.h"

#include "gl_common.h"   // GLEW (Windows) must be included before other GL users
#include <SDL.h>
#include <imgui_impl_sdl2.h>
#include <stdexcept>
#include <iostream>
#include <string>

namespace materializr {

Window::Window(int width, int height, const std::string& title)
    : m_width(width), m_height(height) {

#if defined(__ANDROID__)
    // Stop SDL from synthesizing mouse events from touch. On Android that
    // synthesis leaves ImGui's mouse button stuck "down" after a tap (so every
    // gesture reads as click-and-hold). We feed ImGui clean finger events
    // ourselves in pollEvents() instead.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        throw std::runtime_error(std::string("Failed to initialize SDL: ") + SDL_GetError());
    }

    // Request the right GL context per platform. Desktop: GL 3.3 Core. Android:
    // GL ES 3.0 (same shader/feature subset Materializr uses).
#if defined(__ANDROID__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN;
#if defined(__ANDROID__)
    // On a phone the app owns the whole screen; SDL reports the real size back.
    flags |= SDL_WINDOW_FULLSCREEN;
#else
    flags |= SDL_WINDOW_RESIZABLE;
#endif

    m_window = SDL_CreateWindow(title.c_str(),
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                m_width, m_height, flags);
    if (!m_window) {
        SDL_Quit();
        throw std::runtime_error(std::string("Failed to create SDL window: ") + SDL_GetError());
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        throw std::runtime_error(std::string("Failed to create GL context: ") + SDL_GetError());
    }
    SDL_GL_MakeCurrent(m_window, static_cast<SDL_GLContext>(m_glContext));

#ifdef _WIN32
    // Load GL 3.3 core entry points (no-op on Linux/Android, which export them).
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        throw std::runtime_error("Failed to initialize GLEW (OpenGL loader)");
    }
#endif

    SDL_GL_SetSwapInterval(1); // vsync

    // Reflect the actual created size (Android fullscreen overrides the request).
    SDL_GetWindowSize(m_window, &m_width, &m_height);
}

Window::~Window() {
    if (m_glContext) SDL_GL_DeleteContext(static_cast<SDL_GLContext>(m_glContext));
    if (m_window) SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void Window::swapBuffers() {
    SDL_GL_SwapWindow(m_window);
}

void Window::pollEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
#if defined(__ANDROID__)
        // Touch -> ImGui mouse, handled directly (SDL's own synthesis is off).
        // Single-finger touch drives the left mouse button: position first, then
        // press on down / release on up, so taps and drags both behave correctly.
        if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP) {
            ImGuiIO& tio = ImGui::GetIO();
            float x = e.tfinger.x * tio.DisplaySize.x;
            float y = e.tfinger.y * tio.DisplaySize.y;
            tio.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            tio.AddMousePosEvent(x, y);
            if (e.type == SDL_FINGERDOWN)    tio.AddMouseButtonEvent(0, true);
            else if (e.type == SDL_FINGERUP) tio.AddMouseButtonEvent(0, false);
            continue;   // don't also route finger events through the backend
        }
#endif
        // Feed every event to ImGui (handles mouse, keyboard, text).
        ImGui_ImplSDL2_ProcessEvent(&e);
        switch (e.type) {
            case SDL_QUIT:
                m_shouldClose = true;
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_CLOSE &&
                    e.window.windowID == SDL_GetWindowID(m_window)) {
                    m_shouldClose = true;
                }
                break;
            default:
                break;
        }
    }
    SDL_GetWindowSize(m_window, &m_width, &m_height);
}

void Window::framebufferSize(int& w, int& h) const {
    SDL_GL_GetDrawableSize(m_window, &w, &h);
}

float Window::uiScale() const {
#if defined(__ANDROID__)
    // Scale the desktop-density UI up for a touch screen. Use the physical DPI
    // against a 96-dpi desktop baseline (so a 240-dpi tablet -> 2.5x), clamped.
    float ddpi = 240.0f, hdpi = 0.0f, vdpi = 0.0f;
    if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) != 0 || ddpi <= 0.0f) ddpi = 240.0f;
    float s = ddpi / 120.0f;    // 240-dpi tablet -> 2.0x (was 2.5x, a bit too big)
    if (s < 1.4f) s = 1.4f;     // never smaller than 1.4x on a touch device
    if (s > 2.5f) s = 2.5f;
    return s;
#else
    return 1.0f;                 // desktop UI is already correctly sized
#endif
}

bool Window::isCtrlDown() {
#if defined(__ANDROID__)
    return false; // no hardware modifier keys on touch; multi-select uses a toggle
#else
    const Uint8* state = SDL_GetKeyboardState(nullptr);
    return state[SDL_SCANCODE_LCTRL] || state[SDL_SCANCODE_RCTRL];
#endif
}

} // namespace materializr
