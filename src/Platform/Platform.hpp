#pragma once
// Private OS layer. Implemented once per OS: Platform_mac.mm (Cocoa), Platform_win.cpp (Win32).
// All coordinates are logical px. Screen space: origin top-left of the primary display, y down.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "dawn/webgpu_cpp.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSize.h"
#include "terracotta2/Core/Application.hpp"
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2::platform {

struct NativeWindow; // opaque, per OS

// Implemented by Window::Impl; the platform calls these from the event pump.
struct WindowEvents {
    virtual ~WindowEvents() = default;
    virtual void onResize() = 0;       // content size or DPI scale changed
    virtual void onRedraw() = 0;       // OS wants content now (e.g. during macOS live resize)
    virtual void onMouseMove(float x, float y) = 0;
    virtual void onMouseButton(float x, float y, MouseButton button, bool down, int mods) = 0;
    virtual void onMouseLeave() = 0;
    virtual void onScroll(float dx, float dy) = 0; // logical px; dy > 0 = scroll up
    virtual void onKey(Key key, KeyAction action, int mods) = 0;
    virtual void onChar(uint32_t codepoint) = 0;
    virtual void onCloseRequest() = 0; // OS close button; the window is NOT destroyed by the platform
    virtual bool isCaption(float, float) { return false; }        // custom titlebar: does this point drag the window?
    virtual bool isMaximizeButton(float, float) { return false; } // custom titlebar: over WindowButtons' maximize (Snap Layouts)
    virtual void onActivate(bool) {}                              // became / stopped being the active (key) window
};

void init(); // once, from Application(): app object, menu (Cmd+Q), DPI awareness, window class
void shutdown();

// `owner`: main window for floating windows, the owning window for popups, else null. Floating
// windows use ordinary z-order. Popups (desc.popup) are created invisible; showPopup() reveals them.
NativeWindow* createWindow(const WindowDesc& desc, WindowEvents* events, NativeWindow* owner);
void showPopup(NativeWindow* w);              // without activating it or taking key focus
void setContentSize(NativeWindow* w, SkSize logical);
SkRect workArea(SkPoint screen);              // usable area (no menu bar / taskbar) of the display containing `screen`
void setOnDeactivate(std::function<void()> fn); // the app stopped being the active app

// Custom titlebars. startWindowDrag hands the current mouse press to the OS window drag (macOS; a
// double-click zooms or minimizes per the system setting). On Windows caption drags never reach the
// client area (WM_NCHITTEST answers HTCAPTION via isCaption), so it does nothing there.
void startWindowDrag(NativeWindow* w);
void minimize(NativeWindow* w);
void toggleMaximize(NativeWindow* w);
bool isMaximized(NativeWindow* w);
void destroyWindow(NativeWindow* w);
wgpu::Surface createSurface(const wgpu::Instance& instance, NativeWindow* w);

SkISize framebufferSize(NativeWindow* w); // device px
SkSize logicalSize(NativeWindow* w);
float contentScale(NativeWindow* w);
SkPoint contentScreenPos(NativeWindow* w); // top-left of the content area
void setContentScreenPos(NativeWindow* w, SkPoint p);
void setTitle(NativeWindow* w, const std::string& title);
void raise(NativeWindow* w);
void setCursor(NativeWindow* w, Cursor c);
std::vector<NativeWindow*> windowsFrontToBack(); // this app's windows only

// Event pump. waitEvents blocks until at least one event (or wake()) then drains the queue.
void waitEvents();
void pollEvents();
void wake(); // thread-safe

int modifiers(); // Mod flags currently held

std::string clipboard();
void setClipboard(const std::string& text);

} // namespace Terracotta2::platform
