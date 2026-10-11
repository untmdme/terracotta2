# Terracotta2

Terracotta2 is a small retained-mode C++23 UI library. You describe a tree of widgets once and update only what changes. It uses three libraries:

- **Yoga** for layout. Every widget is a flexbox container.
- **Skia** (Graphite) for drawing and text.
- **Dawn** for the GPU: Metal on macOS, D3D12 on Windows.

Windows only repaint the regions that changed, so an idle app uses 0% CPU.

## Building

You need CMake 3.30 or newer, git, and a C++23 compiler (deducing `this` is required). vcpkg is a git submodule: the first configure fetches it, bootstraps it and installs Yoga, Skia and Dawn from `vcpkg.json`. That first run takes a while.

```bash
git clone <repo-url> terracotta2 && cd terracotta2
cmake --preset debug             # or: release
cmake --build --preset debug -j
open build/debug/app/Hello_World.app   # macOS; Windows: build\debug\app\Hello_World.exe
```

Two presets, each with its own build directory:

- `debug` (`build/debug`): your code at `-O0 -g` with asserts enabled. It is CMake's `RelWithDebInfo` configuration with debug flags, not `Debug`: vcpkg's Skia package drops its libraries and defines in a configuration named `Debug` when only release libraries are installed. On Windows, set the flags to `/Od /Zi` instead.
- `release` (`build/release`): optimized, `NDEBUG`.

Both link the same release-built Skia, Dawn and Yoga (one vcpkg install shared by both); debug builds of Skia and Dawn would take hours to build and run very slowly. The presets target Apple Silicon macOS (`arm64-osx-release`). On Windows, change the triplet in `CMakePresets.json` (e.g. `x64-windows-release`).

To add your own app, create a `.cpp` file in `app/` and register it in `app/CMakeLists.txt`:

```cmake
add_executable(My_App my_app.cpp)
target_link_libraries(My_App PRIVATE Terracotta2)
```

## Your first window

This program opens a window with a greeting, a text field and a click counter. It is `app/hello_world.cpp`, trimmed down:

```cpp
#include <terracotta2/Core/Application.hpp>
#include <terracotta2/Core/Window.hpp>
#include <terracotta2/UI/Container.hpp>
#include <terracotta2/UI/Controls.hpp>
#include <string>

using namespace Terracotta2;

int main() {
    Application app; // platform + GPU; one per process
    Window* window = app.createWindow({.title = "Hello", .width = 480, .height = 300});

    int clicks = 0;
    window->getRootWidget()->add<Panel>().grow(1).padding(24).gap(12).build([&](Panel& page) {
        auto& greeting = page.add<Label>("Hello, world!").fontSize(28).weight(700);
        auto& counter = page.add<Label>("Clicked 0 times").color(SK_ColorGRAY);

        page.add<InputText>("Your name", [g = &greeting](const std::string& name) {
            g->text("Hello, " + (name.empty() ? std::string("world") : name) + "!");
        });
        page.add<Button>("Click me", [&clicks, c = &counter] {
            c->text("Clicked " + std::to_string(++clicks) + " times");
        });
    });

    app.run(); // returns when the window closes
}
```

1. `Application` sets up the platform and GPU. Create exactly one per process.
2. `createWindow` takes a `WindowDesc` with `title`, `width` and `height` in logical pixels. The first window is the main window, and closing it ends the app.
3. `getRootWidget()` returns the window's root widget. You add your UI under it.
4. `app.run()` runs the event loop. It sleeps while nothing changes.

Widgets keep their own state. To change one after it is built, keep a reference to it (like `greeting` above) and call a setter. The window re-lays out and repaints only what changed.

## Layout basics

Every widget is a flexbox container, laid out by Yoga with the same rules as CSS flexbox. You build the UI from three operations:

- `add<T>(args...)` creates a child of type `T`, passes `args` to its constructor, and returns a `T&`.
- **Setters chain** and return the widget's own type, so `add<Label>("Hi").fontSize(20).grow(1)` still gives you a `Label&`.
- `build(fn)` calls `fn(widget)` so you can nest children in code that is shaped like the tree.

```cpp
root.add<VBox>().padding(16).gap(8).build([](VBox& col) {
    col.add<Label>("Title").fontSize(26).weight(700);
    col.add<HBox>().gap(8).build([](HBox& row) {
        row.add<Button>("OK", [] { /* ... */ });
        row.add<Button>("Cancel", [] { /* ... */ });
    });
});
```

`VBox` stacks its children vertically and `HBox` places them in a row. `Panel` is a vertical box that also paints a background color.

These layout setters work on every widget:

| Setter | What it does |
| --- | --- |
| `grow(f)` / `shrink(f)` / `basis(px)` | Flex grow, shrink and basis |
| `width(px)` / `height(px)` / `size(w, h)` | Fixed size |
| `widthPercent(p)` / `heightPercent(p)` | Size as a percentage of the parent |
| `minSize(w, h)` / `maxSize(w, h)` | Size limits |
| `padding(px)` / `padding(YGEdge, px)` | Inner spacing, on all edges or one |
| `margin(px)` / `margin(YGEdge, px)` | Outer spacing |
| `gap(px)` | Space between children |
| `direction(YGFlexDirection)` | Row or column |
| `alignItems(YGAlign)` / `alignSelf(YGAlign)` / `justify(YGJustify)` | Alignment |
| `wrap(YGWrap)` | Wrap children onto new lines |
| `absolute(x, y)` | Take the widget out of the flow and place it at (x, y) |
| `visible(bool)` | Hide the widget. Hidden widgets take no space. |

Widgets size themselves to their content by default. Use `.grow(1)` to make one fill the space that is left, for example a page that should fill the window.

If you need a handle to a widget from inside a chain, use `ref`:

```cpp
Label* status = nullptr;
page.add<Label>("Ready").ref(status);
```

## Built-in widgets

Events are plain callbacks that you pass to the constructor. There is no signal or slot system.

| Widget | Create with | Useful setters and methods |
| --- | --- | --- |
| `Label` | `Label(text)` | `text`, `fontSize`, `weight` (100 to 900), `color`, `lineHeight`, `font`, `selectable` |
| `Button` | `Button(text, onClick)` | None. `onClick` is a `std::function<void()>` |
| `InputText` | `InputText(placeholder, onChange)` | `value()`, `setValue`. `onChange` gets the new text on every edit |
| `VBox` / `HBox` | `VBox()` / `HBox()` | Layout setters only |
| `Panel` | `Panel(color)` | `background` |
| `ScrollView` | `ScrollView()` | `scrollY`, `setScrollY`. Add children directly. |
| `Canvas` | `Canvas(fn)` | `fn(canvas, bounds)` draws with Skia |

Labels wrap to the width their container gives them. To use your own font, call `loadFont("path.ttf")` and pass the family name it returns to `.font()`.

Colors and sizes for every widget come from `theme()` (see `Theme/Theme.hpp`).

For a custom widget, subclass `Widget` and override `onDraw`, `onMouseDown`, `onKey` and the other event methods. Your subclass gets all the layout setters automatically. After changing what you draw, call `markPaintDirty()`, or `markLayoutDirty()` if the size changed.

## Next steps

The example apps in `app/` are the best reference after this page:

| File | What it shows |
| --- | --- |
| `hello_world.cpp` | The smallest useful app (the walkthrough above) |
| `widget_gallery.cpp` | Every built-in widget, text styles, scrolling and simple custom widgets |
| `docking_example.cpp` | A docked workspace that doubles as a layout, text and custom drawing tutorial |
| `media_example.cpp` | Image (PNG) and SVG icon widgets written outside the library, and background loading on a worker thread |
| `benchmark.cpp` | Frame timings for layout, paint and submit (run with `--interactive` to view a scene) |

For the full picture, read the guide: `docs/Terracotta2-Guide.epub`. It covers how every part of the library works and why, with recipes for custom widgets. Its sources are in `docs/book/`; rebuild it with `python3 docs/book/build_epub.py`.

The headers in `include/terracotta2/` are commented. `UI/Widget.hpp` covers layout, input, animation and manual layout.
