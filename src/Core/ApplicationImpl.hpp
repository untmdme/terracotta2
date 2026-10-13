#pragma once
// Private: shared by Application.cpp and Window.cpp (Window reaches the GPU objects through here).
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "dawn/webgpu_cpp.h"
#include "include/gpu/graphite/Context.h"

#include "../Platform/Platform.hpp"
#include "terracotta2/Core/Application.hpp"

namespace Terracotta2 {

struct Application::Impl {
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    std::unique_ptr<skgpu::graphite::Context> context; // shared by every window (each has its own Recorder)

    std::vector<std::unique_ptr<Window>> windows; // creation order
    std::unordered_map<const Window*, platform::NativeWindow*> natives; // Window::Window/teardown keep this in sync
    std::vector<Window*> pendingClose;
    Window* main = nullptr;
    bool quitting = false;

    std::function<bool(Window*, float, float, bool)> mouseHook;

    bool dragActive = false;
    std::function<void(SkPoint)> dragMove, dragRelease;

    // The open popup window, if any (one at a time, like native menus). While it is open, a press or
    // scroll in any other window closes it (and is swallowed), and keys typed in any window go to it.
    Window* popup = nullptr;
    std::function<void()> closePopup;
};

} // namespace Terracotta2
