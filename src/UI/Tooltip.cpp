#include "Tooltip.hpp"
#include <utility>
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include "terracotta2/UI/Label.hpp"
#include "terracotta2/UI/Popup.hpp"

namespace Terracotta2::tooltip {
namespace {

// The tip itself: text on a box, the whole content of its popup window.
class TipBox : public Widget {
public:
    explicit TipBox(const std::string& text) {
        const auto& st = theme().tooltip;
        padding(Edge::Horizontal, 8).padding(Edge::Vertical, 4).maxWidth(320);
        add<Label>(text).fontSize(st.fontSize).color(st.text);
    }
    void onDraw(SkCanvas* c) override {
        const auto& st = theme().tooltip;
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(st.background);
        c->drawRect(m_bounds, p); // the popup window's corners are rounded
        p.setStyle(SkPaint::kStroke_Style);
        p.setColor(st.border);
        c->drawRRect(SkRRect::MakeRectXY(m_bounds.makeInset(0.5f, 0.5f), 4, 4), p);
    }
};

// Per-window tooltip state: a hidden child of the root created on first hover, which runs the show
// delay (animate) and owns the tip's popup window while it is up. The owner pointer is only compared,
// never dereferenced.
class Layer : public Widget {
public:
    Layer() {
        inspect(Inspect::Hide);
        absolute(0, 0);
        setVisible(false);
    }
    ~Layer() override { hide(true); }
    static Layer* find(Widget* root) {
        for (auto& c : root->children())
            if (auto* l = dynamic_cast<Layer*>(c.get())) return l;
        return nullptr;
    }

    void hover(const Widget* owner, const std::string& text, SkPoint at) {
        if (owner == m_owner) return; // still on the same widget: stay put
        hide(true);
        m_owner = owner;
        if (!owner) return;
        m_text = text;
        m_at = at;
        animate(theme().tooltip.delaySec, nullptr, [this] { show(); });
    }
    void hide(bool forget) {
        m_anim.stop();
        if (m_tip) Application::get().closeWindow(std::exchange(m_tip, nullptr)); // get(): may run during shutdown
        if (forget) m_owner = nullptr;
    }
    Widget* hitTest(float, float) override { return nullptr; }

private:
    void show() {
        // Anchored to a 20 px band at the pointer: below it, clear of the cursor; above it at the screen bottom.
        m_tip = Overlay::createWindow(*window(), SkRect::MakeXYWH(m_at.x(), m_at.y(), 0, 20), 4);
        m_tip->root()->add<TipBox>(m_text);
    }

    const Widget* m_owner = nullptr;
    std::string m_text;
    SkPoint m_at{0, 0};
    Window* m_tip = nullptr; // its popup window while shown
};

} // namespace

void hover(Widget* root, Widget* hovered, SkPoint at) {
    if (!root) return;
    Widget* owner = hovered;
    while (owner && owner->tooltip().empty()) owner = owner->parent();
    Layer* layer = Layer::find(root);
    if (!layer) {
        if (!owner) return;
        layer = &root->add<Layer>();
    }
    layer->hover(owner, owner ? owner->tooltip() : std::string(), at);
}

void hide(Widget* root, bool forget) {
    if (Layer* layer = root ? Layer::find(root) : nullptr) layer->hide(forget);
}

} // namespace Terracotta2::tooltip
