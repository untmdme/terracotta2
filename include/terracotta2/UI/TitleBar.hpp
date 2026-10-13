#pragma once
#include "terracotta2/UI/Box.hpp"

namespace Terracotta2 {

// Your own title bar for a window created with WindowDesc::customTitlebar: a row at the top of the
// root. Its empty parts (and labels, icons, boxes inside it) drag the window, and a double-click
// maximizes it, natively on each OS; buttons and other widgets that take input stay clickable.
// On macOS it leaves room on the left for the native traffic lights.
//
//   window->root()->add<TitleBar>().build([](TitleBar& bar) {
//       bar.add<Label>("My App").grow(1);
//       bar.add<WindowButtons>(); // minimize / maximize / close on Windows; nothing on macOS
//   });
class TitleBar : public HBox {
public:
    TitleBar();
    void onDraw(SkCanvas* canvas) override;
};

// Minimize, maximize/restore and close buttons for a custom title bar on Windows. Takes no space on
// macOS, which keeps its own buttons. Hovering maximize opens the Windows 11 Snap Layouts flyout.
class WindowButtons : public HBox {
public:
    WindowButtons();
    Widget* maximizeButton() const { return m_maximize; } // null on macOS

private:
    Widget* m_maximize = nullptr;
};

} // namespace Terracotta2
