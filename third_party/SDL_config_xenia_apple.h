/*
  Xenia Apple SDL override (macOS + iOS):
  Strip SDL down to audio (CoreAudio) + joystick (IOKit on macOS, MFi via
  GameController). Video is dummy-only; Xenia drives its own Cocoa/UIKit/
  Metal window management.
*/

#include <TargetConditionals.h>

#if TARGET_OS_IPHONE

#include "SDL2/include/SDL_config_iphoneos.h"

#undef SDL_VIDEO_DRIVER_UIKIT
#undef SDL_SENSOR_COREMOTION

#else  /* macOS */

#include "SDL2/include/SDL_config_macosx.h"

#undef SDL_VIDEO_DRIVER_COCOA
#undef SDL_VIDEO_DRIVER_X11
#undef SDL_VIDEO_DRIVER_X11_DYNAMIC
#undef SDL_VIDEO_DRIVER_X11_DYNAMIC_XEXT
#undef SDL_VIDEO_DRIVER_X11_DYNAMIC_XINPUT2
#undef SDL_VIDEO_DRIVER_X11_DYNAMIC_XRANDR
#undef SDL_VIDEO_DRIVER_X11_DYNAMIC_XSS
#undef SDL_VIDEO_DRIVER_X11_XDBE
#undef SDL_VIDEO_DRIVER_X11_XINPUT2
#undef SDL_VIDEO_DRIVER_X11_XRANDR
#undef SDL_VIDEO_DRIVER_X11_XSCRNSAVER
#undef SDL_VIDEO_DRIVER_X11_XSHAPE
#undef SDL_VIDEO_DRIVER_X11_HAS_XKBKEYCODETOKEYSYM
#undef SDL_VIDEO_DRIVER_X11_SUPPORTS_GENERIC_EVENTS
#undef SDL_VIDEO_OPENGL
#undef SDL_VIDEO_OPENGL_ES2
#undef SDL_VIDEO_OPENGL_EGL
#undef SDL_VIDEO_OPENGL_CGL
#undef SDL_VIDEO_OPENGL_GLX
#undef SDL_VIDEO_VULKAN
#undef SDL_VIDEO_METAL
#undef SDL_VIDEO_RENDER_OGL
#undef SDL_VIDEO_RENDER_OGL_ES2
#undef SDL_VIDEO_RENDER_METAL
#undef SDL_PLATFORM_SUPPORTS_METAL

/* Use dummy haptic instead of IOKit ForceFeedback — Xenia does not rumble
   via the SDL_Haptic* API. (SDL_GameControllerRumble still works through
   the joystick driver's direct ForceFeedback calls.) */
#undef SDL_HAPTIC_IOKIT
#ifndef SDL_HAPTIC_DUMMY
#define SDL_HAPTIC_DUMMY 1
#endif

#ifndef SDL_VIDEO_DRIVER_DUMMY
#define SDL_VIDEO_DRIVER_DUMMY 1
#endif

#endif  /* TARGET_OS_IPHONE */
