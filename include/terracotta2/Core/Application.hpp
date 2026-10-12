#pragma once
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "include/core/SkPoint.h"

namespace Terracotta2 {

class Window;

struct WindowDesc {
    std::string title = "Terracotta2";
    int width = 1280, height = 720;       // logical px (content area)
    float x = NAN, y = NAN;               // screen logical px of the content area; NAN = centered
    bool floating = false;                // undocked dock window (ordinary z-order; closing re-docks its tabs)
};

// Process-wide singleton: native platform (Cocoa / Win32), the shared GPU device
// (Dawn + one Skia Graphite Context for all windows) and the event loop.
//
// Screen coordinates are logical px, origin top-left of the primary display, y down.
class Application {
public:
    Application();
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    static Application& get(); // the live instance

    // The first window created is the main window; closing it ends run().
    Window* createWindow(const WindowDesc& desc);
    // Deferred: the window is destroyed at the end of the current loop iteration.
    void closeWindow(Window* window);
    Window* mainWindow() const;
    std::vector<Window*> windows() const; // front-to-back z-order

    // Blocks while idle (0% CPU); pumps frames while any window animates.
    void run();
    void quit();
    // One loop iteration: process OS events (blocking only if `block` and idle),
    // render windows that need a frame, destroy closed windows. Returns false once quitting.
    bool pumpOnce(bool block);
    void wake(); // unblock a blocking wait (thread-safe)

    // ---- global pointer grab for cross-window drags ----
    // While active, mouse moves and the button release from ANY window are routed here
    // (screen coords) instead of to widgets. Widget mouse capture is released in all windows.
    void beginGlobalDrag(std::function<void(SkPoint screen)> onMove, std::function<void(SkPoint screen)> onRelease);
    void cancelGlobalDrag();
    bool isGlobalDragActive() const;

    // Topmost window whose content area contains `screen`, skipping `exclude`.
    Window* windowAt(SkPoint screen, const Window* exclude = nullptr) const;

    // Debug tools (inspector picking): sees every mouse move (press = false) and button press
    // (press = true) in window coords before the widgets do; return true to swallow it.
    void setMouseHook(std::function<bool(Window*, float x, float y, bool press)> hook);

    int modifiers() const; // Mod flags held right now (e.g. Ctrl+scroll in onScroll)

    std::string clipboard() const;
    void setClipboard(const std::string& text);

private:
    friend class Window;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Terracotta2
