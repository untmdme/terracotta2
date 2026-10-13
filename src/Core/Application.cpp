#include "terracotta2/Core/Application.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/dawn/DawnBackendContext.h"
#include "include/gpu/graphite/dawn/DawnGraphiteTypes.h"

#include "ApplicationImpl.hpp"
#include "terracotta2/Core/Window.hpp"

namespace Terracotta2 {

namespace {
Application* s_instance = nullptr;
std::string str(wgpu::StringView s) { return std::string(s.data ? s.data : "", s.data ? s.length : 0); }
} // namespace

Application::Application() : m_impl(std::make_unique<Impl>()) {
    Impl& d = *m_impl;
    platform::init();
    try {
        wgpu::InstanceFeatureName timedWait = wgpu::InstanceFeatureName::TimedWaitAny;
        wgpu::InstanceDescriptor idesc;
        idesc.requiredFeatureCount = 1;
        idesc.requiredFeatures = &timedWait;
        d.instance = wgpu::CreateInstance(&idesc);
        if (!d.instance) throw std::runtime_error("wgpu::CreateInstance failed");

        // No surface exists yet, so no compatibleSurface (fine on Metal; every surface is compatible).
        wgpu::RequestAdapterOptions aopts;
        aopts.powerPreference = wgpu::PowerPreference::HighPerformance;
        std::string err;
        d.instance.WaitAny(
            d.instance.RequestAdapter(&aopts, wgpu::CallbackMode::WaitAnyOnly,
                                      [&](wgpu::RequestAdapterStatus s, wgpu::Adapter a, wgpu::StringView msg) {
                                          if (s == wgpu::RequestAdapterStatus::Success) d.adapter = std::move(a);
                                          else err = str(msg);
                                      }),
            UINT64_MAX);
        if (!d.adapter) throw std::runtime_error("RequestAdapter failed: " + err);

        // Optional features Skia Graphite can exploit.
        std::vector<wgpu::FeatureName> features;
        for (auto f : {wgpu::FeatureName::Depth32FloatStencil8, wgpu::FeatureName::TransientAttachments,
                       wgpu::FeatureName::DawnLoadResolveTexture, wgpu::FeatureName::DawnPartialLoadResolveTexture,
                       wgpu::FeatureName::MSAARenderToSingleSampled, wgpu::FeatureName::BGRA8UnormStorage,
                       wgpu::FeatureName::ImplicitDeviceSynchronization})
            if (d.adapter.HasFeature(f)) features.push_back(f);

        wgpu::DeviceDescriptor ddesc;
        ddesc.requiredFeatureCount = features.size();
        ddesc.requiredFeatures = features.data();
        ddesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
                                    [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView msg) {
                                        if (reason != wgpu::DeviceLostReason::Destroyed &&
                                            reason != wgpu::DeviceLostReason::FailedCreation)
                                            std::fprintf(stderr, "[wgpu] device lost: %s\n", str(msg).c_str());
                                    });
        ddesc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView msg) {
            std::fprintf(stderr, "[wgpu] error: %s\n", str(msg).c_str());
        });
        err.clear();
        d.instance.WaitAny(
            d.adapter.RequestDevice(&ddesc, wgpu::CallbackMode::WaitAnyOnly,
                                    [&](wgpu::RequestDeviceStatus s, wgpu::Device dev, wgpu::StringView msg) {
                                        if (s == wgpu::RequestDeviceStatus::Success) d.device = std::move(dev);
                                        else err = str(msg);
                                    }),
            UINT64_MAX);
        if (!d.device) throw std::runtime_error("RequestDevice failed: " + err);
        d.queue = d.device.GetQueue();

        skgpu::graphite::DawnBackendContext backend;
        backend.fInstance = d.instance;
        backend.fDevice = d.device;
        backend.fQueue = d.queue;
        d.context = skgpu::graphite::ContextFactory::MakeDawn(backend, skgpu::graphite::ContextOptions());
        if (!d.context) throw std::runtime_error("Graphite MakeDawn failed");
    } catch (...) {
        d.context.reset();
        d.queue = nullptr;
        d.device = nullptr;
        d.adapter = nullptr;
        d.instance = nullptr;
        platform::shutdown();
        throw;
    }
    s_instance = this;
    // Switching to another app closes an open popup, as native menus do.
    platform::setOnDeactivate([&d] {
        if (auto close = d.closePopup) close();
    });
}

Application::~Application() {
    Impl& d = *m_impl;
    d.quitting = true;
    d.dragActive = false;
    d.dragMove = d.dragRelease = nullptr;
    // Newest first (floating tool windows before the main window); every Window releases its
    // GPU objects before the shared context/device go away.
    while (!d.windows.empty()) {
        std::unique_ptr<Window> w = std::move(d.windows.back());
        d.windows.pop_back();
        w.reset();
    }
    d.main = nullptr;
    d.context.reset();
    d.queue = nullptr;
    d.device = nullptr;
    d.adapter = nullptr;
    d.instance = nullptr;
    platform::shutdown();
    s_instance = nullptr;
}

Application& Application::get() {
    if (!s_instance) throw std::runtime_error("Application::get(): no live Application");
    return *s_instance;
}

Window* Application::createWindow(const WindowDesc& desc) {
    Impl& d = *m_impl;
    std::unique_ptr<Window> w(new Window(*this, desc));
    Window* raw = w.get();
    d.windows.push_back(std::move(w));
    if (!d.main) d.main = raw;
    return raw;
}

void Application::closeWindow(Window* window) {
    Impl& d = *m_impl;
    if (window && std::find(d.pendingClose.begin(), d.pendingClose.end(), window) == d.pendingClose.end())
        d.pendingClose.push_back(window);
    // pumpOnce() sees the pending close and does not block, so no wake is needed on the UI thread.
}

Window* Application::mainWindow() const { return m_impl->main; }

namespace {
template <class I> // I = Application::Impl (private type, so deduced rather than named)
Window* windowFor(const I& d, platform::NativeWindow* n) {
    for (auto& w : d.windows)
        if (d.natives.at(w.get()) == n) return w.get();
    return nullptr;
}
} // namespace

std::vector<Window*> Application::windows() const {
    const Impl& d = *m_impl;
    std::vector<Window*> out;
    for (auto* n : platform::windowsFrontToBack())
        if (Window* w = windowFor(d, n)) out.push_back(w);
    for (auto& w : d.windows) // hidden/minimized ones the OS did not list
        if (std::find(out.begin(), out.end(), w.get()) == out.end()) out.push_back(w.get());
    return out;
}

Window* Application::windowAt(SkPoint screen, const Window* exclude) const {
    const Impl& d = *m_impl;
    for (auto* n : platform::windowsFrontToBack()) {
        Window* w = windowFor(d, n);
        if (!w || w == exclude) continue;
        if (w->screenFrame().contains(screen.x(), screen.y())) return w;
    }
    return nullptr;
}

void Application::run() {
    while (pumpOnce(true)) {}
}

void Application::quit() {
    m_impl->quitting = true;
    wake(); // callable from the Cocoa/Win32 callbacks and other threads; pumpOnce also checks the flag before blocking
}

void Application::wake() { platform::wake(); }

void Application::post(std::function<void()> fn) { m_impl->posted.push_back(std::move(fn)); }

int Application::setTimer(double seconds, std::function<void()> fn) {
    Impl& d = *m_impl;
    const int id = d.nextTimerId++;
    const auto due = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                            std::chrono::duration<double>(std::max(seconds, 0.0)));
    d.timers.push_back({id, due, std::move(fn)});
    return id;
}

void Application::cancelTimer(int id) {
    std::erase_if(m_impl->timers, [id](const Impl::Timer& t) { return t.id == id; });
}

bool Application::pumpOnce(bool block) {
    Impl& d = *m_impl;
    bool busy = d.quitting || !d.pendingClose.empty() || !d.posted.empty();
    for (auto& w : d.windows)
        if (w->needsFrame()) busy = true;
    const auto now = std::chrono::steady_clock::now();
    double timeout = -1; // until the next timer is due
    for (auto& t : d.timers) {
        const double in = std::chrono::duration<double>(t.due - now).count();
        timeout = timeout < 0 ? std::max(in, 0.0) : std::min(timeout, std::max(in, 0.0));
    }
    if (block && !busy && timeout != 0) platform::waitEvents(timeout);
    else platform::pollEvents();

    // Due timers. Moved out first: callbacks may set or cancel timers.
    std::vector<Impl::Timer> due;
    const auto after = std::chrono::steady_clock::now();
    for (auto it = d.timers.begin(); it != d.timers.end();) {
        if (it->due <= after) { due.push_back(std::move(*it)); it = d.timers.erase(it); }
        else ++it;
    }
    for (auto& t : due) t.fn();

    std::vector<std::function<void()>> posted;
    posted.swap(d.posted);
    for (auto& fn : posted) fn(); // may post more: those run next iteration

    std::vector<Window*> snapshot; // callbacks may create windows
    for (auto& w : d.windows) snapshot.push_back(w.get());
    for (Window* w : snapshot)
        if (std::find_if(d.windows.begin(), d.windows.end(), [w](auto& u) { return u.get() == w; }) != d.windows.end())
            w->renderFrame();

    // Deferred closes. The list is moved out first: destructors may close further windows.
    std::vector<Window*> pending = std::move(d.pendingClose);
    d.pendingClose.clear();
    for (Window* w : pending) {
        auto it = std::find_if(d.windows.begin(), d.windows.end(), [w](auto& u) { return u.get() == w; });
        if (it == d.windows.end()) continue;
        std::unique_ptr<Window> dying = std::move(*it);
        d.windows.erase(it);
        if (w == d.main) {
            d.main = nullptr;
            d.quitting = true;
        }
        dying.reset();
    }
    return !d.quitting;
}

void Application::beginGlobalDrag(std::function<void(SkPoint)> onMove, std::function<void(SkPoint)> onRelease) {
    Impl& d = *m_impl;
    d.dragActive = true;
    d.dragMove = std::move(onMove);
    d.dragRelease = std::move(onRelease);
    for (auto& w : d.windows) w->releaseCapture();
}

void Application::cancelGlobalDrag() {
    m_impl->dragActive = false;
    m_impl->dragMove = m_impl->dragRelease = nullptr;
}

bool Application::isGlobalDragActive() const { return m_impl->dragActive; }

void Application::setMouseHook(std::function<bool(Window*, float, float, bool)> hook) { m_impl->mouseHook = std::move(hook); }

int Application::modifiers() const { return platform::modifiers(); }
std::string Application::clipboard() const { return platform::clipboard(); }
void Application::setClipboard(const std::string& text) { platform::setClipboard(text); }

} // namespace Terracotta2
