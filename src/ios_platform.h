#pragma once
#include "platform_defs.h"

// iOS runtime services (implemented in ios_platform.mm). Safe to include
// everywhere: on non-iOS platforms the query is an inline constant so callers
// need no guards.
namespace materializr {

#if defined(MZ_IOS)

// Point the process at the app bundle + sandbox before Application starts:
// chdir to the bundle (so cwd-relative "assets/fonts/<name>" resolves), set the
// CSF_* env vars at the bundled OCCT resources, ensure $HOME/.config exists for
// settings, and install the UIKit lifecycle watch backing iosInBackground().
void iosInitRuntime();

// True between SDL_APP_WILLENTERBACKGROUND and SDL_APP_DIDENTERFOREGROUND.
// iOS terminates apps that touch the GL context while backgrounded, so the
// render loop must stop drawing whenever this is set (see Application::run).
bool iosInBackground();

#else

inline bool iosInBackground() { return false; }

#endif

} // namespace materializr
