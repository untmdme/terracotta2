#include "terracotta2/Core/Window.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

#include <yoga/Yoga.h>

#include "dawn/webgpu_cpp.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/GraphiteTypes.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Recording.h"
#include "include/gpu/graphite/Surface.h"
#include "include/gpu/graphite/dawn/DawnBackendContext.h"
#include "include/gpu/graphite/dawn/DawnGraphiteTypes.h"

#include "ApplicationImpl.hpp"
#include "terracotta2/Theme/Theme.hpp"

namespace Terracotta2 {

// DrawCounter.cpp: a canvas forwarding to `target` that counts draws into *count.
std::unique_ptr<SkCanvas> makeCountingCanvas(SkCanvas* target, int* count);

namespace {

std::vector<Widget*> chainOf(Widget* w) {
    std::vector<Widget*> c;
    for (; w; w = w->parent()) c.push_back(w);
    return c;
}
bool contains(const std::vector<Widget*>& v, Widget* w) { return std::find(v.begin(), v.end(), w) != v.end(); }

// The event as `w` sees it: in w's coordinate space (differs from the window only under a childTransform).
MouseEvent localTo(const Widget* w, MouseEvent ev) {
    const SkPoint p = w->mapFromWindow({ev.x, ev.y});
    ev.x = p.x();
    ev.y = p.y();
    return ev;
}

constexpr wgpu::TextureFormat kFormat = wgpu::TextureFormat::BGRA8Unorm;


} // namespace

struct Window::Impl : platform::WindowEvents {
    Window* self = nullptr;
    Application* app = nullptr;
    Application::Impl* A = nullptr;
    platform::NativeWindow* native = nullptr;
    bool floating = false;
    bool ready = false; // OS callbacks arriving during construction are ignored

    wgpu::Surface surface;
    std::unique_ptr<skgpu::graphite::Recorder> recorder;
    wgpu::Texture backbuffer;
    sk_sp<SkSurface> skSurface;

    std::unique_ptr<Widget> root;
    FrameStats stats;
    std::vector<SkRect> damage; // logical px, repainted separately
    int fbW = 0, fbH = 0; // framebuffer px
    float scale = 1.0f;
    SkSize logical{0, 0};
    bool showDamage = false;
    bool tearingDown = false;
    std::function<void()> onClose;
    std::function<void()> onFrame;

    std::vector<Widget*> animating;
    std::chrono::steady_clock::time_point lastTick = std::chrono::steady_clock::now();

    Widget* hovered = nullptr;
    Widget* captured = nullptr;
    Widget* pressed = nullptr; // hit widget at mouse-down, for onClick
    Widget* focused = nullptr;
    Widget* highlight = nullptr;
    Theme* theme = nullptr; // own theme, or null for appTheme()
    Cursor curCursor = Cursor::Arrow;

    // ---- GPU ----
    void configureSurface() {
        wgpu::SurfaceConfiguration cfg;
        cfg.device = A->device;
        cfg.format = kFormat;
        cfg.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst;
        cfg.width = (uint32_t)fbW;
        cfg.height = (uint32_t)fbH;
        cfg.presentMode = wgpu::PresentMode::Fifo;
        cfg.alphaMode = wgpu::CompositeAlphaMode::Opaque;
        surface.Configure(&cfg);
    }

    void createBackbuffer() {
        skSurface.reset();
        wgpu::TextureDescriptor td;
        td.size = {(uint32_t)fbW, (uint32_t)fbH, 1};
        td.format = kFormat;
        td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding |
                   wgpu::TextureUsage::CopySrc;
        backbuffer = A->device.CreateTexture(&td);
        auto bt = skgpu::graphite::BackendTextures::MakeDawn(backbuffer.Get());
        skSurface = SkSurfaces::WrapBackendTexture(recorder.get(), bt, nullptr, nullptr);
        if (!skSurface) throw std::runtime_error("Failed to wrap backbuffer as SkSurface");
    }

    // Re-reads window metrics and rebuilds size-dependent state. Safe to call at any time.
    void resize() {
        SkISize fb = platform::framebufferSize(native);
        SkSize lg = platform::logicalSize(native);
        float sc = platform::contentScale(native);
        if (fb.width() <= 0 || fb.height() <= 0 || lg.width() <= 0 || lg.height() <= 0) { // minimized
            fbW = fbH = 0;
            return;
        }
        if (fb.width() == fbW && fb.height() == fbH && sc == scale && lg == logical) return;
        fbW = fb.width();
        fbH = fb.height();
        scale = sc;
        logical = lg;
        configureSurface();
        createBackbuffer();
        if (root) root->size(logical.width(), logical.height()); // ponytail: fixed-size root; flags layout dirty
        damage = {SkRect::MakeWH(logical.width(), logical.height())};
    }

    void teardown() {
        ready = false;
        tearingDown = true; // forgetSubtree() becomes a no-op while the tree dies
        hovered = captured = focused = highlight = nullptr;
        animating.clear();
        root.reset(); // widgets first: their destructors call back into us
        if (A->context) A->context->submit(skgpu::graphite::SyncToCpu::kYes);
        skSurface.reset();
        recorder.reset();
        backbuffer = nullptr;
        surface = nullptr;
        A->natives.erase(self);
        if (native) platform::destroyWindow(native);
        native = nullptr;
    }

    // ---- input helpers ----
    void updateCursor() {
        Cursor c = Cursor::Arrow;
        if (captured) {
            c = captured->cursor();
        } else {
            for (Widget* w = hovered; w; w = w->parent())
                if (w->cursor() != Cursor::Arrow) { c = w->cursor(); break; }
        }
        if (c == curCursor) return;
        curCursor = c;
        platform::setCursor(native, c);
    }

    // Chain semantics: a widget stays hovered while any descendant is hovered.
    void updateHover(float x, float y) {
        Widget* nh = root ? root->hitTest(x, y) : nullptr;
        if (nh == hovered) return;
        auto oldChain = chainOf(hovered);
        auto newChain = chainOf(nh);
        hovered = nh;
        for (Widget* w : oldChain)
            if (!contains(newChain, w)) w->onMouseLeave();
        for (auto it = newChain.rbegin(); it != newChain.rend(); ++it)
            if (!contains(oldChain, *it)) (*it)->onMouseEnter();
    }

    void tickAnimations() {
        auto now = std::chrono::steady_clock::now();
        float dt = std::min(std::chrono::duration<float>(now - lastTick).count(), 0.1f);
        lastTick = now;
        auto snapshot = animating; // callbacks may add/remove widgets
        for (Widget* w : snapshot) {
            if (!contains(animating, w)) continue;
            w->tickAnimation(dt);
            if (!w->isAnimating()) animating.erase(std::remove(animating.begin(), animating.end(), w), animating.end());
        }
    }

    // ---- platform::WindowEvents ----
    void onResize() override {
        if (ready) resize();
    }
    void onRedraw() override {
        if (!ready) return;
        resize();
        self->renderFrame();
    }
    void onMouseMove(float x, float y) override {
        if (ready) self->injectMouseMove(x, y);
    }
    void onMouseButton(float x, float y, MouseButton b, bool down, int mods) override {
        if (ready) self->injectMouseButton(x, y, b, down, mods);
    }
    void onMouseLeave() override {
        if (ready) self->injectMouseLeave();
    }
    void onScroll(float dx, float dy) override {
        if (ready) self->injectScroll(dx, dy);
    }
    void onKey(Key k, KeyAction a, int mods) override {
        if (ready) self->injectKey(k, a, mods);
    }
    void onChar(uint32_t cp) override {
        if (ready) self->injectChar(cp);
    }
    void onCloseRequest() override {
        if (!ready) return;
        if (onClose) {
            auto h = onClose; // the handler may replace/clear itself
            h();
        }
        app->closeWindow(self);
    }
};

Window::Window(Application& app, const WindowDesc& desc) : m_impl(std::make_unique<Impl>()) {
    Impl& d = *m_impl;
    d.self = this;
    d.app = &app;
    d.A = app.m_impl.get();
    Application::Impl& A = *d.A;
    try {
        platform::NativeWindow* owner = (desc.floating && A.main) ? A.natives.at(A.main) : nullptr;
        d.floating = owner != nullptr;
        d.native = platform::createWindow(desc, &d, owner);
        if (!d.native) throw std::runtime_error("platform::createWindow failed");
        A.natives[this] = d.native;

        d.surface = platform::createSurface(A.instance, d.native);
        if (!d.surface) throw std::runtime_error("Failed to create wgpu::Surface");

        d.recorder = A.context->makeRecorder();
        if (!d.recorder) throw std::runtime_error("Graphite makeRecorder failed");

        d.root = std::make_unique<Widget>();
        d.root->m_host = this;
        d.root->direction(YGFlexDirectionColumn);
        const char* env = std::getenv("TC2_SHOW_DAMAGE");
        d.showDamage = env && env[0] == '1';

        d.resize();
        d.ready = true;
    } catch (...) {
        // Partial construction: ~Window won't run, so tear down here.
        d.teardown();
        throw;
    }
}

Window::~Window() { m_impl->teardown(); }

Widget* Window::getRootWidget() const { return m_impl->root.get(); }
Application& Window::app() const { return *m_impl->app; }
const FrameStats& Window::lastFrameStats() const { return m_impl->stats; }
float Window::contentScale() const { return m_impl->scale; }
SkSize Window::size() const { return m_impl->logical; }
Widget* Window::hoveredWidget() const { return m_impl->hovered; }
Widget* Window::focusedWidget() const { return m_impl->focused; }

bool Window::isFloating() const { return m_impl->floating; }
void Window::setTitle(const std::string& title) { platform::setTitle(m_impl->native, title); }
void Window::raise() { platform::raise(m_impl->native); }
void Window::setOnClose(std::function<void()> handler) { m_impl->onClose = std::move(handler); }
void Window::setOnFrame(std::function<void()> handler) { m_impl->onFrame = std::move(handler); }
void Window::requestClose() { m_impl->onCloseRequest(); }

SkPoint Window::toScreen(SkPoint local) const {
    SkPoint o = platform::contentScreenPos(m_impl->native);
    return {local.x() + o.x(), local.y() + o.y()};
}
SkPoint Window::toLocal(SkPoint screen) const {
    SkPoint o = platform::contentScreenPos(m_impl->native);
    return {screen.x() - o.x(), screen.y() - o.y()};
}
SkRect Window::screenFrame() const {
    SkPoint o = platform::contentScreenPos(m_impl->native);
    return SkRect::MakeXYWH(o.x(), o.y(), m_impl->logical.width(), m_impl->logical.height());
}
void Window::setScreenPosition(SkPoint topLeft) { platform::setContentScreenPos(m_impl->native, topLeft); }

bool Window::needsFrame() const {
    const Impl& d = *m_impl;
    // Minimized (no framebuffer): nothing can be drawn, so never ask the loop to spin.
    return d.fbW > 0 && (!d.damage.empty() || (d.root && d.root->isLayoutDirty()) || !d.animating.empty());
}

// ---- services ----

void Window::addDamage(const SkRect& r) {
    if (r.isEmpty()) return;
    const SkRect o = r.makeOutset(kPaintOverflow, kPaintOverflow);
    auto& list = m_impl->damage;
    for (const SkRect& d : list)
        if (d.contains(o)) return;
    std::erase_if(list, [&](const SkRect& d) { return o.contains(d); });
    // ponytail: past kMaxDamageRects (e.g. hundreds of scattered animations) fall back to one
    // bounding rect; merge nearest pairs instead if such frames get too expensive.
    constexpr size_t kMaxDamageRects = 64;
    if (list.size() >= kMaxDamageRects) {
        SkRect all = o;
        for (const SkRect& d : list) all.join(d);
        list = {all};
        return;
    }
    list.push_back(o);
}

void Window::setFocus(Widget* w) {
    Impl& d = *m_impl;
    if (w == d.focused) return;
    Widget* old = d.focused;
    d.focused = w;
    if (old) old->onBlur();
    if (w) w->onFocus();
}

void Window::setTheme(Theme* t) {
    m_impl->theme = t;
    addDamage(SkRect::MakeSize(m_impl->logical));
}
Theme* Window::theme() const { return m_impl->theme; }

void Window::setHighlight(Widget* w) {
    Impl& d = *m_impl;
    if (w == d.highlight) return;
    if (d.highlight) addDamage(d.highlight->mapToWindow(d.highlight->getBounds()));
    d.highlight = w;
    if (w) addDamage(w->mapToWindow(w->getBounds()));
}
Widget* Window::highlight() const { return m_impl->highlight; }

void Window::releaseCapture() {
    Impl& d = *m_impl;
    if (!d.captured) return;
    d.captured = nullptr;
    d.updateCursor();
}

void Window::startAnimating(Widget* w) {
    Impl& d = *m_impl;
    if (!contains(d.animating, w)) d.animating.push_back(w);
    // No wake needed: widgets run on the UI thread, and Application::pumpOnce re-checks
    // needsFrame() before it blocks.
}

void Window::forgetSubtree(Widget* w) {
    Impl& d = *m_impl;
    if (d.tearingDown) return;
    auto inside = [w](Widget* x) { return x && w->isAncestorOf(x); };
    if (inside(d.hovered)) d.hovered = nullptr;
    if (inside(d.captured)) d.captured = nullptr;
    if (inside(d.pressed)) d.pressed = nullptr;
    if (inside(d.focused)) d.focused = nullptr;
    if (inside(d.highlight)) d.highlight = nullptr;
    d.animating.erase(std::remove_if(d.animating.begin(), d.animating.end(), inside), d.animating.end());
}

// ---- rendering ----

bool Window::renderFrame() {
    Impl& d = *m_impl;
    ThemeScope themeScope(d.theme);
    if (!d.ready) return false;
    d.resize(); // catches size changes whose callbacks have not fired yet
    if (!d.root || d.fbW <= 0 || !needsFrame()) return false;

    auto t0 = std::chrono::steady_clock::now();
    FrameStats stats;
    stats.frameIndex = d.stats.frameIndex;

    d.tickAnimations();

    auto tLayout = std::chrono::steady_clock::now();
    auto msSince = [](auto t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); };
    if (d.root->isLayoutDirty()) {
        YGNodeCalculateLayout(d.root->getYGNode(), d.logical.width(), d.logical.height(), YGDirectionLTR);
        d.root->applyLayout(0, 0);
        d.root->m_layout_dirty = false;
        stats.layoutRan = true;
    }
    stats.layoutMs = msSince(tLayout);

    // Snap each rect outward to whole device pixels so clip edges are exact.
    const float s = d.scale;
    std::vector<SkRect> rects;
    for (const SkRect& r : d.damage) {
        SkIRect dev = SkRect::MakeLTRB(r.fLeft * s, r.fTop * s, r.fRight * s, r.fBottom * s).roundOut();
        if (!dev.intersect(SkIRect::MakeWH(d.fbW, d.fbH))) continue;
        rects.push_back(SkRect::MakeLTRB(dev.fLeft / s, dev.fTop / s, dev.fRight / s, dev.fBottom / s));
        stats.damage.join(rects.back());
        stats.damageArea += rects.back().width() * rects.back().height();
    }
    d.damage.clear();
    stats.damageRects = int(rects.size());
    if (rects.empty()) {
        d.stats = stats;
        return false;
    }

    auto tPaint = std::chrono::steady_clock::now();
    const auto counting = makeCountingCanvas(d.skSurface->getCanvas(), &stats.drawCalls);
    SkCanvas* canvas = counting.get();
    canvas->save();
    canvas->scale(s, s);
    // Each rect on its own: overlapping rects repaint their overlap twice, with identical result.
    for (const SkRect& r : rects) {
        canvas->save();
        canvas->clipRect(r);
        canvas->drawColor(Terracotta2::theme().globalBackground, SkBlendMode::kSrc);
        d.root->paint(canvas, r, stats.widgetsPainted);
        if (d.highlight) { // inside its bounds, so its own damage covers it
            SkPaint p;
            const SkRect hb = d.highlight->mapToWindow(d.highlight->getBounds());
            p.setColor(SkColorSetARGB(40, 255, 0, 255));
            canvas->drawRect(hb, p);
            p.setStyle(SkPaint::kStroke_Style);
            p.setColor(SkColorSetRGB(255, 0, 255));
            canvas->drawRect(hb.makeInset(0.5f, 0.5f), p);
        }
        canvas->restore();
    }
    if (d.showDamage) {
        static const SkColor palette[] = {SK_ColorRED, SK_ColorGREEN, SK_ColorYELLOW, SK_ColorCYAN, SK_ColorMAGENTA};
        SkPaint p;
        p.setStyle(SkPaint::kStroke_Style);
        p.setStrokeWidth(1.0f / s);
        p.setColor(palette[stats.frameIndex % 5]);
        for (const SkRect& r : rects) canvas->drawRect(r.makeInset(0.5f / s, 0.5f / s), p);
    }
    canvas->restore();
    stats.paintMs = msSince(tPaint);

    auto tSubmit = std::chrono::steady_clock::now();
    skgpu::graphite::Context* ctx = d.A->context.get();
    auto recording = d.recorder->snap();
    if (recording) {
        skgpu::graphite::InsertRecordingInfo info;
        info.fRecording = recording.get();
        ctx->insertRecording(info);
    }
    ctx->submit(skgpu::graphite::SyncToCpu::kNo);

    // Blit the whole backbuffer to the swapchain (swapchain contents are not preserved).
    wgpu::SurfaceTexture st;
    d.surface.GetCurrentTexture(&st);
    auto ok = [&] {
        return st.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal ||
               st.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal;
    };
    if (!ok()) {
        d.configureSurface();
        d.surface.GetCurrentTexture(&st);
    }
    if (ok()) {
        wgpu::CommandEncoder enc = d.A->device.CreateCommandEncoder();
        wgpu::TexelCopyTextureInfo src, dst;
        src.texture = d.backbuffer;
        dst.texture = st.texture;
        wgpu::Extent3D extent{(uint32_t)d.fbW, (uint32_t)d.fbH, 1};
        enc.CopyTextureToTexture(&src, &dst, &extent);
        wgpu::CommandBuffer cmd = enc.Finish();
        d.A->queue.Submit(1, &cmd);
        d.surface.Present();
    } else {
        // ponytail: retry next loop iteration; backbuffer is already up to date
        d.damage = {SkRect::MakeWH(d.logical.width(), d.logical.height())};
    }

    d.A->instance.ProcessEvents();
    ctx->checkAsyncWorkCompletion();
    stats.submitMs = msSince(tSubmit);

    stats.cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    stats.frameIndex++;
    d.stats = stats;
    const bool presented = ok();
    if (presented && d.onFrame) {
        auto f = d.onFrame; // the handler may replace itself
        f();
    }
    return presented;
}

// ---- input ----

void Window::injectMouseMove(float x, float y) {
    Impl& d = *m_impl;
    ThemeScope themeScope(d.theme);
    Application::Impl& A = *d.A;
    if (A.dragActive) {
        auto cb = A.dragMove; // copy: the callback may end the drag
        if (cb) cb(toScreen({x, y}));
        return;
    }
    if (auto hook = A.mouseHook; hook && hook(this, x, y, false)) return; // copy: it may clear itself
    MouseEvent ev{x, y, MouseButton::Left, 0};
    if (d.captured) {
        d.captured->onMouseMove(localTo(d.captured, ev));
    } else {
        d.updateHover(x, y);
        for (Widget* w = d.hovered; w;) {
            Widget* next = w->parent();
            if (w->onMouseMove(localTo(w, ev))) break;
            w = next;
        }
    }
    d.updateCursor();
}

void Window::injectMouseButton(float x, float y, MouseButton button, bool down, int mods) {
    Impl& d = *m_impl;
    ThemeScope themeScope(d.theme);
    Application::Impl& A = *d.A;
    if (A.dragActive) {
        if (down) return;
        // End the session BEFORE the callback so it may start another drag or destroy windows.
        auto cb = std::move(A.dragRelease);
        A.dragActive = false;
        A.dragMove = nullptr;
        A.dragRelease = nullptr;
        if (cb) cb(toScreen({x, y}));
        return;
    }
    if (auto hook = A.mouseHook; down && hook && hook(this, x, y, true)) return;
    MouseEvent ev{x, y, button, mods};
    if (down) {
        if (d.captured) return; // another button is mid-drag
        d.updateHover(x, y);
        Widget* target = d.root ? d.root->hitTest(x, y) : nullptr;
        d.pressed = target;
        Widget* f = target;
        while (f && !f->isFocusable()) f = f->parent();
        setFocus(f);
        for (Widget* w = target; w;) {
            Widget* next = w->parent();
            if (w->onMouseDown(localTo(w, ev))) {
                d.captured = w;
                break;
            }
            w = next;
        }
    } else {
        Widget* c = d.captured;
        d.captured = nullptr;
        if (c) {
            c->onMouseUp(localTo(c, ev));
        } else {
            for (Widget* w = d.hovered; w;) {
                Widget* next = w->parent();
                if (w->onMouseUp(localTo(w, ev))) break;
                w = next;
            }
        }
        // Click: deepest widget containing both the press and the release target.
        // (onMouseUp may have destroyed widgets; forgetSubtree nulls `pressed` then.)
        Widget* p = d.pressed;
        d.pressed = nullptr;
        Widget* w = p && d.root ? d.root->hitTest(x, y) : nullptr;
        while (w && !w->isAncestorOf(p)) w = w->parent();
        while (w) {
            Widget* next = w->parent();
            if (w->onClick(localTo(w, ev))) break;
            w = next;
        }
        d.updateHover(x, y);
    }
    d.updateCursor();
}

void Window::injectMouseLeave() {
    Impl& d = *m_impl;
    ThemeScope themeScope(d.theme);
    if (d.captured) return; // a drag keeps its target until release
    d.updateHover(-1, -1);
    d.updateCursor();
}

void Window::injectScroll(float dx, float dy) {
    ThemeScope themeScope(m_impl->theme);
    for (Widget* w = m_impl->hovered; w;) {
        Widget* next = w->parent();
        if (w->onScroll(dx, dy)) break;
        w = next;
    }
}

void Window::injectKey(Key key, KeyAction action, int mods) {
    ThemeScope themeScope(m_impl->theme);
    KeyEvent ev{key, action, mods};
    for (Widget* w = m_impl->focused; w;) {
        Widget* next = w->parent();
        if (w->onKey(ev)) break;
        w = next;
    }
}

void Window::injectChar(uint32_t codepoint) {
    ThemeScope themeScope(m_impl->theme);
    for (Widget* w = m_impl->focused; w;) {
        Widget* next = w->parent();
        if (w->onChar(codepoint)) break;
        w = next;
    }
}

} // namespace Terracotta2
