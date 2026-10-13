#include "terracotta2/UI/Popup.hpp"
#include "../Core/ApplicationImpl.hpp"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"

namespace Terracotta2 {

namespace {

// Root content of a popup window: knows its Overlay, and closes it on Escape (keys reach the popup's
// focused widget first, e.g. a menu, and bubble up to here).
class PopupHost : public Widget {
public:
    explicit PopupHost(Overlay& overlay) : m_overlay(overlay) {}
    Overlay& overlay() const { return m_overlay; }
    bool isFocusable() const override { return true; }
    bool isTabStop() const override { return false; }
    bool onKey(const KeyEvent& e) override {
        if (e.key != Key::Escape || e.action != KeyAction::Press) return false;
        m_overlay.close();
        return true;
    }

private:
    Overlay& m_overlay;
};

} // namespace

Overlay::Overlay() {
    inspect(Inspect::Hide);
    absolute(0, 0);
    setVisible(false); // nothing to show in its own window: it only owns the popup window
}

Overlay::~Overlay() { close(); }

Overlay& Overlay::of(Widget& w) {
    Widget* root = w.window()->root();
    for (auto& c : root->children())
        if (auto* h = dynamic_cast<PopupHost*>(c.get())) return h->overlay();
    for (auto& c : root->children())
        if (auto* o = dynamic_cast<Overlay*>(c.get())) return *o;
    return root->add<Overlay>();
}

Window* Overlay::createWindow(Window& owner, const SkRect& anchor, float cornerRadius) {
    const SkPoint screen = owner.toScreen({anchor.left(), anchor.top()});
    Window* w = owner.app().createWindow({.title = "Popup", .width = 1, .height = 1, .x = screen.x(), .y = screen.y(), .popup = true,
                                          .anchorHeight = anchor.height(), .owner = &owner, .cornerRadius = cornerRadius});
    w->setTheme(&theme()); // the opener's: callers run inside its (widget or window) theme
    return w;
}

Widget& Overlay::host(const SkRect& anchor) {
    close();
    m_popup = createWindow(*window(), anchor, theme().popup.cornerRadius);
    Application::Impl& A = *window()->app().m_impl;
    A.popup = m_popup;
    A.closePopup = [this] { close(); };
    PopupHost& h = m_popup->root()->add<PopupHost>(*this);
    m_popup->setFocus(&h);
    return h;
}

void Overlay::close() {
    if (!m_popup) return;
    Window* p = std::exchange(m_popup, nullptr);
    // Application::get(), not p->app(): during shutdown the popup window may already be gone.
    Application& app = Application::get();
    Application::Impl& A = *app.m_impl;
    if (A.popup == p) {
        A.popup = nullptr;
        A.closePopup = nullptr; // callers invoke a copy, so clearing it here is safe
    }
    app.closeWindow(p); // deferred: widgets in the popup may still be running a callback
}

void PopupBox::onDraw(SkCanvas* c) {
    const auto& st = theme().popup;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(st.background);
    c->drawRect(m_bounds, p); // the window's own corners are rounded
    p.setStyle(SkPaint::kStroke_Style);
    p.setColor(st.border);
    c->drawRRect(SkRRect::MakeRectXY(m_bounds.makeInset(0.5f, 0.5f), st.cornerRadius, st.cornerRadius), p);
}

} // namespace Terracotta2
