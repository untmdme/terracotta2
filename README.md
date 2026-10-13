# Terracotta2

Terracotta2 is a small retained-mode C++23 UI library. You describe a tree of widgets once and update only what changes. It uses three libraries:

- **Yoga** for layout. Every widget is a flexbox container.
- **Skia** (Graphite) for drawing and text.
- **Dawn** for the GPU: Metal on macOS, D3D12 on Windows.

Windows only repaint the regions that changed, so an idle app uses 0% CPU.

**Status:** pre-1.0. The API still changes between versions. Supported platforms are macOS on Apple Silicon and Windows (x64, ARM64). Licensed under MIT (see `LICENSE`).

## Building

You need CMake 3.30 or newer and a C++23 compiler (deducing `this` is required). Yoga, Skia (Graphite + Dawn) and Dawn come prebuilt in `deps/<platform>`: static, release-only libraries with their CMake config files. Nothing is downloaded or built from source.

```bash
git clone <repo-url> terracotta2 && cd terracotta2
cmake --preset debug             # or: release
cmake --build --preset debug -j
open build/debug/app/Hello_World.app   # macOS; Windows: build\debug\app\Hello_World.exe
```

Two presets, each with its own build directory:

- `debug` (`build/debug`): your code at `-O0 -g` with asserts enabled. It is CMake's `RelWithDebInfo` configuration with debug flags, not `Debug`: the prebuilt Skia package drops its libraries and defines in a configuration named `Debug`, since only release libraries are shipped. On Windows the same preset uses `/Od /Zi`.
- `release` (`build/release`): optimized, `NDEBUG`.

Both link the same release-built Skia, Dawn and Yoga. The dependency folder is picked from the host: `deps/arm64-osx` on macOS, `deps/x64-windows` or `deps/arm64-windows` on Windows. Override it with `-DTC2_DEPS_DIR=...`. Each folder is a vcpkg install tree (`vcpkg_installed/<triplet>` without `tools/`) built from the `arm64-osx-release`, `x64-windows-static-md-release` and `arm64-windows-static-md` triplets. Keep them static: a DLL build of Skia has no SVG module and does not export the skparagraph symbols. The Windows `.lib` files are stripped of their `/Z7` debug info (`llvm-objcopy --strip-debug --remove-section=.chks64`), and `webgpu_dawn.lib` is split into `webgpu_dawn.lib` + `webgpu_dawn_<n>.lib` so every file stays under GitHub's 100 MB limit; `CMakeLists.txt` links the extra parts.

`build/<preset>/app/Core_Test` checks focus order, list virtualization, multi-line editing, menu keys and tooltips in a live window. `Node_Editor_Test` checks the node editor's model, and `Node_Editor --bench` drives the whole editor through scripted input.

To add your own app, create a `.cpp` file in `app/` and register it in `app/CMakeLists.txt`:

```cmake
add_executable(My_App my_app.cpp)
target_link_libraries(My_App PRIVATE Terracotta2)
```

## Using it in your own project

Terracotta2 is a CMake library target. When it is not the top-level project, it does not build the example apps (set `TC2_BUILD_APPS=ON` to build them anyway). It still adds its `deps/<platform>` folder to `CMAKE_PREFIX_PATH`, so Yoga, Skia and Dawn are found without extra setup.

As a subdirectory or git submodule:

```cmake
add_subdirectory(external/terracotta2)
target_link_libraries(My_App PRIVATE Terracotta2)
```

Or install it (`cmake --install build/release --prefix <dir>`) and find it:

```cmake
find_package(Terracotta2 CONFIG REQUIRED)
target_link_libraries(My_App PRIVATE Terracotta2::Terracotta2)
```

Then include everything with `#include <terracotta2/Terracotta2.hpp>`.

## Your first window

This program opens a window with a greeting, a text field and a click counter. It is `app/hello_world.cpp`, trimmed down:

```cpp
#include <terracotta2/Terracotta2.hpp>
#include <string>

using namespace Terracotta2;

int main() {
    Application app; // platform + GPU; one per process
    Window* window = app.createWindow({.title = "Hello", .width = 480, .height = 300});

    int clicks = 0;
    window->root()->add<Panel>().grow(1).padding(24).gap(12).build([&](Panel& page) {
        auto& greeting = page.add<Label>("Hello, world!").fontSize(28).weight(700);
        auto& counter = page.add<Label>("Clicked 0 times").color(SK_ColorGRAY);

        page.add<InputText>("Your name").onChange([g = &greeting](const std::string& name) {
            g->text("Hello, " + (name.empty() ? std::string("world") : name) + "!");
        });
        page.add<Button>("Click me").onClick([&clicks, c = &counter] {
            c->text("Clicked " + std::to_string(++clicks) + " times");
        });
    });

    app.run(); // returns when the window closes
}
```

1. `Application` sets up the platform and GPU. Create exactly one per process.
2. `createWindow` takes a `WindowDesc` with `title`, `width` and `height` in logical pixels. The first window is the main window, and closing it ends the app.
3. `root()` returns the window's root widget. You add your UI under it.
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
        row.add<Button>("OK").onClick([] { /* ... */ });
        row.add<Button>("Cancel").onClick([] { /* ... */ });
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
| `minSize(w, h)` / `maxSize(w, h)` / `maxWidth(px)` / `maxHeight(px)` | Size limits |
| `padding(px)` / `padding(Edge, px)` | Inner spacing, on all edges or one (`Edge::Left`, `Edge::Horizontal`, ...) |
| `margin(px)` / `margin(Edge, px)` | Outer spacing |
| `gap(px)` | Space between children |
| `direction(Direction)` | `Direction::Row` or `Direction::Column` |
| `alignItems(Align)` / `alignSelf(Align)` | Cross-axis alignment: `Align::Start`, `Center`, `End`, `Stretch`, `Baseline` |
| `justify(Justify)` | Main-axis distribution: `Justify::Start`, `Center`, `End`, `SpaceBetween`, `SpaceAround`, `SpaceEvenly` |
| `wrap(bool)` | Wrap children onto new lines |
| `absolute(x, y)` | Take the widget out of the flow and place it at (x, y) |
| `visible(bool)` | Hide the widget. Hidden widgets take no space. |

For any Yoga style these don't cover, `yogaNode()` gives you the node; call `markLayoutDirty()` after changing it.

Widgets size themselves to their content by default. Use `.grow(1)` to make one fill the space that is left, for example a page that should fill the window.

If you need a handle to a widget from inside a chain, use `ref`:

```cpp
Label* status = nullptr;
page.add<Label>("Ready").ref(status);
```

## Built-in widgets

Events are plain callbacks set with chainable setters, and there is no signal or slot system. Controls with a value all work the same way: `value()` reads it, `value(v)` sets it without firing the callback, and `onChange(fn)` fires when the user edits it.

```cpp
page.add<Button>("Save").onClick([] { /* ... */ });
page.add<Slider>(0, 100).value(50).onChange([](float v) { /* ... */ });
```

| Widget | Create with | Useful setters and methods |
| --- | --- | --- |
| `Label` | `Label(text)` | `text`, `fontSize`, `weight` (100 to 900), `color`, `lineHeight`, `font`, `selectable` |
| `Button` | `Button(text)` | `onClick(fn)` |
| `InputText` | `InputText(placeholder)` | `value`, `onChange(fn)` gets the new text on every edit |
| `TextArea` | `TextArea(placeholder)` | Multi-line `InputText`: grows with its text from `rows(n)` to `maxRows(n)` lines, then scrolls inside |
| `Toggle` | `Toggle(on)` | `value`, `onChange(fn)` |
| `Checkbox` | `Checkbox(text, checked)` | `value`, `onChange(fn)` |
| `Slider` | `Slider(min, max)` | `value`, `onChange(fn)`. Arrow keys step it when focused |
| `ProgressBar` | `ProgressBar()` | `value` (0 to 1) |
| `Dropdown` | `Dropdown(items, selected)` | `value` (an index), `onChange(fn)` |
| `VBox` / `HBox` | `VBox()` / `HBox()` | Layout setters only |
| `Panel` | `Panel(color)` | `background` |
| `ScrollView` | `ScrollView()` | `scrollY`, `setScrollY`, `scrollToShow`. Add children directly |
| `VirtualList` | `VirtualList(rowHeight)` | `rows(makeRow, bindRow)`, `count`, `refresh`, `scrollToRow`. Only the rows in view exist |
| `Canvas` | `Canvas(fn)` | `fn(canvas, bounds)` draws with Skia |
| `DragNumber` | `DragNumber(get, set, min, max)` | Drag horizontally to edit a number. `refresh` re-reads `get` |
| `ColorPicker` | `ColorPicker(get, set)` | HSV square, hue and alpha bars, hex field. `refresh` re-reads `get` |
| `DockSpace` | `DockSpace()` | `splitHorizontal`, `splitVertical`, then `addTab` on the returned nodes, `customFloatingTitlebar()`, `setCentral(content)` (your own UI in the middle, tabs dock on its edges), `dock()`, `revealTab()`, `closeTab()`. Tabs can be dragged into new windows (native title bar, or ImGui-style with the tab bar as title bar) |

`DragNumber` and `ColorPicker` are editors bound to data that lives elsewhere, so they read and write through `get` and `set` functions instead of holding a value. Call `refresh()` when that data changes.

**Tooltips.** Any widget takes `.tooltip("text")`. It shows after the pointer rests on the widget for `theme().tooltip.delaySec`, and a click hides it.

**Menus and popups.** Menus, dropdown lists and tooltips open in their own borderless OS windows, like native menus, so they can extend past the edge of the app window. A popup sizes itself to its content, opens below what it's attached to (or above it near the bottom of the screen), and stays on screen.

- `openMenu(widget, anchor, items, onPick)` opens a menu. Pass the pointer position for a context menu, or a widget's bounds for a button's menu. Arrow keys move through it, Enter picks and Escape closes.
- For your own popups, `Overlay::of(widget).open<PopupBox>(anchor)` returns a styled box to add widgets to.
- A click or scroll anywhere else, Escape, or switching to another app closes the popup. While it's open, keys go to it, and the widget that opened it keeps keyboard focus.

**Keyboard.** Tab and Shift+Tab move focus between buttons, fields and other controls, and scroll the focused one into view. Space or Enter clicks a focused button.

**Custom title bars.** Create the window with `.customTitlebar = true` and add a `TitleBar` as the root's first child. Your widgets go inside it. Its empty parts and plain widgets such as labels drag the window, and a double-click maximizes it, using the OS's own behavior on each platform. Buttons and other interactive widgets in it stay clickable. macOS keeps its native traffic-light buttons, and `TitleBar` leaves room for them. On Windows, add `WindowButtons` at the end for minimize, maximize and close. Colors come from `theme().titleBar`.

```cpp
Window* window = app.createWindow({.title = "My App", .customTitlebar = true});
window->root()->add<TitleBar>().build([](TitleBar& bar) {
    bar.add<Label>("My App").grow(1);
    bar.add<WindowButtons>(); // nothing on macOS
});
```

**Headers.** `<terracotta2/Terracotta2.hpp>` includes everything. Each widget also has its own header in `terracotta2/UI/` (`Button.hpp`, `Slider.hpp`, ...), with closely related ones together: `Box.hpp` (VBox, HBox, Panel), `Toggle.hpp` (Toggle, Checkbox), `Slider.hpp` (Slider, ProgressBar), `InputText.hpp` (InputText, TextArea), `Menu.hpp` (openMenu, Dropdown), `Popup.hpp` (Overlay, PopupBox) and `TitleBar.hpp` (TitleBar, WindowButtons).

Labels wrap to the width their container gives them. To use your own font, call `loadFont("path.ttf")` and pass the family name it returns to `.font()`.

Colors and sizes for every widget come from `theme()` (see `Theme/Theme.hpp`). A window can have its own theme (`Window::setTheme`), and so can any subtree (`Widget::setTheme`): it keeps that theme wherever it moves, e.g. a docked tab dragged into another window. To change a few values for one widget, use `themeOverride`. It edits a copy of the inherited theme, and the copy keeps following the app theme for everything else:

```cpp
row.add<Button>("Pill").themeOverride([](Theme& t) { t.button.cornerRadius = 999; });
```

`openDesigner()` (from `Tools/Designer.hpp`) docks a designer sidebar to the right of the main window, like browser dev tools. Drag its tab out into its own window and back onto the window's right edge. It keeps its own look while you edit the theme. Its Inspector tab shows the widget tree above the selected widget's properties, split like browser dev tools (drag the bar between them). Pick any widget or click it in the tree (Esc cancels or deselects). The selection shows its margin, padding and content, and you can edit its layout live and copy its style as C++. The Theme tab edits every theme value, with a filter.

## Writing your own widgets

Anything the built-in set doesn't cover is a small subclass. Subclass `Widget`, set a default size in the constructor, override the event methods you need and draw with Skia in `onDraw`. This 5-dot rating control is the full widget:

```cpp
class Rating : public Widget {
public:
    Rating() { size(5 * kDot, kDot); }
    Rating& onChange(std::function<void(int)> fn) { m_onChange = std::move(fn); return *this; }
    int value() const { return m_value; }

    bool onMouseClick(const MouseEvent& e) override {
        m_value = std::clamp(int((e.x - m_bounds.left()) / kDot) + 1, 1, 5);
        markPaintDirty(); // repaint this widget only
        if (m_onChange) m_onChange(m_value);
        return true;      // handled: don't bubble to the parent
    }
    Cursor cursor() const override { return Cursor::Hand; }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setAntiAlias(true);
        for (int i = 0; i < 5; ++i) {
            p.setColor(i < m_value ? theme().accent : theme().track);
            c->drawCircle(m_bounds.left() + kDot * (i + 0.5f), m_bounds.centerY(), kDot / 2 - 3, p);
        }
    }

private:
    static constexpr float kDot = 24;
    int m_value = 0;
    std::function<void(int)> m_onChange;
};

// page.add<Rating>().onChange([](int stars) { /* ... */ });
```

What to know:

- Your subclass gets all the layout setters (`grow`, `padding`, ...) and works with `add<T>()`.
- `m_bounds` is the widget's rectangle in window coordinates, and so are mouse event coordinates. Draw inside it: only that area is repainted.
- After changing what you draw, call `markPaintDirty()`. If the size changed too, call `markLayoutDirty()`.
- Event methods return `true` when handled. Returning `true` from `onMouseDown` also captures the mouse until release, which is how drags work. `onMouseClick` fires on press and release over the same widget.
- For keyboard input, return `true` from `isFocusable()` and handle `onKey`. Focusable widgets join the Tab order. Call `drawFocusRing(canvas, radius)` in `onDraw` to show keyboard focus.
- Compose instead of drawing text yourself: add a `Label` as a child in the constructor (`Checkbox` does this).
- Read colors from `theme()` in `onDraw`, so your widget follows theme changes for free.
- For smooth state changes, `animate(seconds, [this](float t) { ...; markPaintDirty(); })`.
- Follow the library's convention for values and events: `value()` reads, `value(v)` sets without firing callbacks, and a chainable `onChange(fn)` fires on user edits.
- For free-form layouts like node graphs or timelines, use `manualLayout()` and place children with `setFrame` (see `NodeGraph` in `docking_example.cpp`).

## Next steps

The example apps in `app/` are the best reference after this page:

| File | What it shows |
| --- | --- |
| `hello_world.cpp` | The smallest useful app (the walkthrough above) |
| `custom_titlebar.cpp` | A window that draws its own title bar, on macOS and Windows |
| `widget_gallery.cpp` | Every built-in widget, text styles, scrolling and a custom animated widget |
| `docking_example.cpp` | A docked workspace that doubles as a layout, text and custom drawing tutorial |
| `media_example.cpp` | Image (PNG) and SVG icon widgets written outside the library (icons are rasterized once per size and shared, so long icon lists stay cheap), and background loading on a worker thread |
| `node_editor/` | A full app: zoomable node graph, timeline, virtual lists, context menus and dropdowns |
| `benchmark.cpp` | Frame timings for layout, paint and submit (run with `--interactive` to view a scene) |

The headers in `include/terracotta2/` are commented. `UI/Widget.hpp` covers layout, input, animation and manual layout.
