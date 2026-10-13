#pragma once
#include <algorithm>
#include "include/core/SkSize.h"
#include "terracotta2/UI/Box.hpp"

namespace Terracotta2 {

// Full-window layer above a window's UI for menus, dropdown lists and pickers. One per window,
// created on first use. A press or scroll outside the popup, or Escape, closes it and is swallowed,
// like a native menu. Opening takes keyboard focus; closing gives it back to where it was.
class Overlay : public Widget {
public:
    Overlay();
    static Overlay& of(Widget& anyWidgetInWindow); // must be attached to a window

    // Replaces the popup with a new T at `at` (window coords), kept inside the window given its
    // expected size: it flips above `at` when there is no room below.
    template <class T, class... A>
    T& open(SkPoint at, SkSize expect, A&&... args) {
        clearChildren();
        T& t = add<T>(std::forward<A>(args)...);
        place(t, at, expect);
        return t;
    }
    void close(); // destroys the popup: never keep a pointer into it past close
    bool isOpen() const { return isVisible(); }

    bool onMouseDown(const MouseEvent&) override;
    bool onScroll(float, float) override;
    bool onKey(const KeyEvent& e) override;
    bool isFocusable() const override { return isOpen(); }
    bool isTabStop() const override { return false; }

private:
    void place(Widget& popup, SkPoint at, SkSize expect);
    Widget* m_returnFocus = nullptr; // focused before open; only compared, never dereferenced unchecked
};

// Styled popup box (theme().popup). Presses inside never reach the Overlay.
class PopupBox : public VBox {
public:
    PopupBox() { padding(4); }
    bool onMouseDown(const MouseEvent&) override { return true; }
    void onDraw(SkCanvas* canvas) override;
};

} // namespace Terracotta2
