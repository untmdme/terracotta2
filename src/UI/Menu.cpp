#include "terracotta2/UI/Menu.hpp"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include "terracotta2/UI/Label.hpp"
#include "terracotta2/UI/Popup.hpp"

namespace Terracotta2 {

namespace {

constexpr float kMenuWidth = 180.0f;

// The menu's box: owns the highlighted ("active") item, which the mouse and the keyboard both move.
class MenuBox : public PopupBox {
public:
    explicit MenuBox(int active) : m_active(active) {}
    int active() const { return m_active; }
    void setActive(int i);
    void pick(int i);
    bool isFocusable() const override { return true; }
    bool isTabStop() const override { return false; }
    bool onKey(const KeyEvent& e) override;

private:
    int m_active;
};

class MenuItem : public HBox {
public:
    MenuItem(const std::string& text, bool current, std::function<void()> fn) : m_fn(std::move(fn)) {
        height(theme().popup.itemHeight).padding(Edge::Horizontal, 10).alignItems(Align::Center);
        Label& l = add<Label>(text);
        if (current) l.color(theme().accent);
    }
    MenuBox& box() const { return static_cast<MenuBox&>(*parent()); }
    int index() const { return int(parent()->indexOf(this)); }
    void run() {
        auto fn = m_fn; // close() destroys this item: touch no members after it
        Overlay::of(*parent()).close();
        fn();
    }
    bool onMouseDown(const MouseEvent&) override { return true; }
    bool onMouseClick(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        run();
        return true;
    }
    void onMouseEnter() override { box().setActive(index()); }
    void onDraw(SkCanvas* c) override {
        if (box().active() != index()) return;
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(theme().popup.itemHover);
        c->drawRRect(SkRRect::MakeRectXY(m_bounds, 4, 4), p);
    }

private:
    std::function<void()> m_fn;
};

void MenuBox::setActive(int i) {
    if (i == m_active) return;
    const int n = int(children().size());
    if (m_active >= 0 && m_active < n) children()[m_active]->markPaintDirty();
    m_active = i;
    if (m_active >= 0 && m_active < n) children()[m_active]->markPaintDirty();
}

void MenuBox::pick(int i) {
    if (i >= 0 && i < int(children().size())) static_cast<MenuItem&>(*children()[i]).run();
}

bool MenuBox::onKey(const KeyEvent& e) {
    if (e.action == KeyAction::Release) return false;
    const int n = int(children().size());
    if (n == 0) return false;
    switch (e.key) {
    case Key::Down: setActive(m_active < 0 ? 0 : (m_active + 1) % n); return true;
    case Key::Up: setActive(m_active <= 0 ? n - 1 : m_active - 1); return true;
    case Key::Home: setActive(0); return true;
    case Key::End: setActive(n - 1); return true;
    case Key::Enter: case Key::Space: pick(m_active); return true;
    default: return false; // Escape bubbles to the Overlay, which closes
    }
}

} // namespace

void openMenu(Widget& from, const SkRect& anchor, const std::vector<std::string>& items, std::function<void(int)> onPick,
              int current) {
    Overlay& overlay = Overlay::of(from);
    // `anchor` is in from's window; the Overlay may belong to another one (from inside a popup).
    const SkPoint local = overlay.window()->toLocal(from.window()->toScreen({anchor.left(), anchor.top()}));
    auto& box = overlay.open<MenuBox>(SkRect::MakeXYWH(local.x(), local.y(), anchor.width(), anchor.height()), current);
    box.minSize(kMenuWidth, 0);
    for (int i = 0; i < int(items.size()); ++i)
        box.add<MenuItem>(items[i], i == current, [onPick, i] { if (onPick) onPick(i); });
    box.window()->setFocus(&box);
}

Dropdown::Dropdown(std::vector<std::string> items, int selected) : m_items(std::move(items)) {
    alignItems(Align::Center).padding(Edge::Right, 20); // room for the chevron
    m_label = &add<Label>("");
    onThemeChanged();
    value(selected);
}

Dropdown& Dropdown::value(int index) {
    m_selected = m_items.empty() ? 0 : std::clamp(index, 0, int(m_items.size()) - 1);
    const std::string& t = m_items.empty() ? std::string() : m_items[m_selected];
    if (m_label->text() != t) m_label->text(t);
    return *this;
}

void Dropdown::openList() {
    // Below the field (or above it near the bottom of the screen); also right inside a zoomed canvas.
    openMenu(*this, mapToWindow(m_bounds).makeOutset(0, 2), m_items, [this](int i) {
        value(i);
        if (m_onChange) m_onChange(i);
    }, m_selected);
}

bool Dropdown::onMouseClick(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    openList();
    return true;
}

bool Dropdown::onKey(const KeyEvent& e) {
    if (e.action != KeyAction::Press || (e.key != Key::Space && e.key != Key::Enter && e.key != Key::Down)) return false;
    openList();
    return true;
}

void Dropdown::onThemeChanged() {
    height(theme().input.height).padding(Edge::Left, theme().input.paddingX);
}

void Dropdown::onDraw(SkCanvas* c) {
    const auto& st = theme().input;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(isHovered() ? theme().button.hoverBackground : st.background);
    c->drawRoundRect(m_bounds, st.cornerRadius, st.cornerRadius, p);
    const float x = m_bounds.right() - 11, y = m_bounds.centerY();
    SkPathBuilder b;
    b.moveTo(x - 4, y - 2);
    b.lineTo(x + 4, y - 2);
    b.lineTo(x, y + 3);
    b.close();
    p.setColor(theme().textMuted);
    c->drawPath(b.detach(), p);
    drawFocusRing(c, st.cornerRadius);
}

} // namespace Terracotta2
