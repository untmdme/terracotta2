#pragma once
#include <utility>
#include "terracotta2/UI/Box.hpp"

namespace Terracotta2 {

class Window;

// Opens popups (menus, dropdown lists, pickers) in their own borderless OS window above the app,
// so they can extend past the window's edges like native menus. One per window, created on first
// use; one popup open at a time per app (opening another closes the first). A press or scroll in
// any other window, Escape, or switching to another app closes it. While it is open, keys typed in
// any of the app's windows go to it; the opener keeps its focus.
class Overlay : public Widget {
public:
    Overlay();
    ~Overlay() override;
    // The window's Overlay. From a widget inside a popup: the Overlay that opened it.
    static Overlay& of(Widget& anyWidgetInWindow); // must be attached to a window

    // Replaces the open popup with a new T (args go to its constructor) and returns it to fill. It
    // opens just below `anchor` (in this Overlay's window coordinates), or above it when there is no
    // room below, and takes the content's natural size. Anchor to a widget's bounds for a dropdown
    // list, or to a point (SkRect::MakeXYWH(x, y, 0, 0)) for a context menu.
    template <class T, class... A>
    T& open(const SkRect& anchor, A&&... args) {
        return host(anchor).template add<T>(std::forward<A>(args)...);
    }
    void close(); // the popup window goes away at the end of this event loop iteration
    bool isOpen() const { return m_popup != nullptr; }
    Window* popupWindow() const { return m_popup; }

    Widget* hitTest(float, float) override { return nullptr; }

    // A bare popup window attached to `anchor` (owner's window coordinates), not registered as the
    // open popup: it takes no keys and doesn't close on outside clicks. Tooltips use it.
    static Window* createWindow(Window& owner, const SkRect& anchor, float cornerRadius);

private:
    Widget& host(const SkRect& anchor);
    Window* m_popup = nullptr; // only compared after close; never dereferenced once closed
};

// Styled popup box (theme().popup): background, border, padding. Fills the popup window, whose
// corners are rounded to theme().popup.cornerRadius.
class PopupBox : public VBox {
public:
    PopupBox() { padding(4); }
    void onDraw(SkCanvas* canvas) override;
};

} // namespace Terracotta2
