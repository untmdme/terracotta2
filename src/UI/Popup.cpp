#include "terracotta2/UI/Popup.hpp"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"

namespace Terracotta2 {

// ================= Overlay =================

Overlay::Overlay() {
    absolute(0, 0).widthPercent(100).heightPercent(100);
    setVisible(false);
}

namespace {
bool treeContains(const Widget* root, const Widget* w) {
    if (root == w) return true;
    for (auto& c : root->children())
        if (treeContains(c.get(), w)) return true;
    return false;
}
} // namespace

Overlay& Overlay::of(Widget& w) {
    Widget* root = w.window()->root();
    for (auto& c : root->children())
        if (auto* o = dynamic_cast<Overlay*>(c.get())) return *o;
    return root->add<Overlay>();
}

void Overlay::place(Widget& popup, SkPoint at, SkSize expect) {
    Window* win = window();
    const SkSize size = win->size();
    const float x = std::clamp(at.x(), 0.0f, std::max(0.0f, size.width() - expect.width()));
    const float y = at.y() + expect.height() <= size.height() ? at.y() : std::max(0.0f, at.y() - expect.height());
    popup.absolute(x, y);
    // Stay above widgets added to the root after the overlay was created.
    Widget* root = parent();
    if (root->children().back().get() != this) root->adopt(root->detach(this));
    visible(true);
    if (Widget* f = win->focusedWidget(); !isAncestorOf(f)) m_returnFocus = f;
    win->setFocus(this); // Escape closes
}

void Overlay::close() {
    if (!isOpen()) return;
    Window* win = window();
    // The opener may have been destroyed meanwhile: only restore a widget still in the tree.
    if (win && isAncestorOf(win->focusedWidget()))
        win->setFocus(m_returnFocus && treeContains(win->root(), m_returnFocus) ? m_returnFocus : nullptr);
    m_returnFocus = nullptr;
    visible(false);
    clearChildren();
}

bool Overlay::onMouseDown(const MouseEvent&) {
    close();
    return true;
}

bool Overlay::onScroll(float, float) {
    close();
    return true;
}

bool Overlay::onKey(const KeyEvent& e) {
    if (e.key != Key::Escape || e.action != KeyAction::Press) return false;
    close();
    return true;
}

// ================= PopupBox / menus =================

void PopupBox::onDraw(SkCanvas* c) {
    const auto& st = theme().popup;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(st.background);
    c->drawRRect(SkRRect::MakeRectXY(m_bounds, st.cornerRadius, st.cornerRadius), p);
    p.setStyle(SkPaint::kStroke_Style);
    p.setColor(st.border);
    c->drawRRect(SkRRect::MakeRectXY(m_bounds.makeInset(0.5f, 0.5f), st.cornerRadius, st.cornerRadius), p);
}

} // namespace Terracotta2
