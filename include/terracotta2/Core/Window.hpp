#pragma once
#include <functional>
#include <memory>
#include <string>
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSize.h"
#include "terracotta2/Core/Application.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2 {

struct FrameStats {
    SkRect damage = SkRect::MakeEmpty(); // bounds of the repainted rects, logical px
    int damageRects = 0;                 // rects repainted this frame
    float damageArea = 0.0f;             // sum of their areas, logical px^2 (overlaps counted twice)
    int widgetsPainted = 0;
    bool layoutRan = false;
    double cpuMs = 0.0;                  // whole renderFrame() on the CPU
    double layoutMs = 0.0;               // Yoga + applyLayout
    double paintMs = 0.0;                // widget onDraw recording (Graphite Recorder)
    double submitMs = 0.0;               // snap + insertRecording + submit + blit + present
    // ponytail: no GPU timestamps; GPU execution time is not measured
    uint64_t frameIndex = 0;
};

// Native OS window (NSWindow / HWND) + GPU surface + retained widget root.
// Created and owned by Application (Application::createWindow).
//
// Rendering: widgets paint into a persistent offscreen texture. Each frame every damaged
// rect is cleared and repainted on its own (clipped), so far-apart changes do not repaint
// what lies between them; then the texture is copied to the swapchain. Nothing changed =>
// no frame at all.
// Debug: env TC2_SHOW_DAMAGE=1 outlines each repainted rect.
class Window {
public:
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Widget* root() const;
    Application& app() const;

    bool needsFrame() const;  // damage pending, layout dirty, or animating
    bool renderFrame();       // lays out + repaints damage if needed; returns true if a frame was presented
    const FrameStats& lastFrameStats() const;

    // ---- OS window ----
    bool isFloating() const;
    void setTitle(const std::string& title);
    SkRect screenFrame() const;              // content area, screen logical px
    void setScreenPosition(SkPoint topLeft); // content-area top-left, screen logical px
    void raise();                            // bring to front
    void minimize();
    void toggleMaximize();                   // maximize (macOS: zoom), or restore
    bool isMaximized() const;
    bool isActive() const;                   // the active (key) window: custom titlebars dim when not
    void close() { app().closeWindow(this); }   // force close: skips the onClose handler
    void requestClose();                          // same as the OS close button: onClose handler, then close
    SkPoint toScreen(SkPoint local) const;
    SkPoint toLocal(SkPoint screen) const;
    // Called when the user clicks the OS close button, before the window is destroyed.
    // Default (no handler): the window closes.
    void setOnClose(std::function<void()> handler);
    // Called after every presented frame (lastFrameStats() is that frame), e.g. for an FPS readout.
    // Changing widgets here schedules another frame: throttle it, or the window never goes idle.
    void setOnFrame(std::function<void()> handler);

    // ---- services for widgets ----
    void addDamage(const SkRect& logicalRect);
    void setFocus(Widget* w);           // nullptr clears; calls onBlur/onFocus
    Widget* focusedWidget() const;
    // Focus the next (or previous) tab stop in tree order, wrapping; scrolls it into view.
    // Tab / Shift+Tab call this when the focused widget doesn't handle the key.
    void focusNext(bool forward = true);
    bool focusVisible() const;          // focus came from the keyboard: widgets draw a focus ring
    Widget* hoveredWidget() const;      // deepest widget under the pointer
    void releaseCapture();              // drop widget mouse capture (no onMouseUp delivered)
    void startAnimating(Widget* w);     // registers w for ticking; wakes the loop
    void forgetSubtree(Widget* w);      // drop hover/focus/capture/animation refs into w's subtree
    // This window's own theme instead of appTheme() (nullptr: the app theme). Only the pointer
    // is kept. In effect for layout, paint and input; build its widgets inside a ThemeScope.
    void setTheme(Theme* t);
    Theme* theme() const;
    // Debug outline drawn over w (inspector tools); nullptr clears. Cleared if w is destroyed.
    void setHighlight(Widget* w);
    Widget* highlight() const;
    float contentScale() const;
    SkSize size() const;                // logical px

    // ---- input entry points (the platform layer routes here; tests may call directly) ----
    // Window-local logical coords. While Application has a global drag active, moves and
    // the release are forwarded to it in screen coords instead of to widgets.
    void injectMouseMove(float x, float y);
    void injectMouseButton(float x, float y, MouseButton button, bool down, int mods = 0);
    void injectMouseLeave();
    void injectScroll(float dx, float dy); // logical px, positive dy = content moves down (scroll up)
    void injectKey(Key key, KeyAction action, int mods);
    void injectChar(uint32_t codepoint);

private:
    friend class Application;
    Window(Application& app, const WindowDesc& desc);
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Terracotta2
