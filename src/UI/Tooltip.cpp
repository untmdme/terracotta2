#include "Tooltip.hpp"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include "terracotta2/UI/Label.hpp"

namespace Terracotta2::tooltip {
namespace {

// One per window, a child of the root created on first hover. Never hit-tested, so it can't
// steal the hover that keeps it open. The owner pointer is only compared, never dereferenced.
class Layer : public Widget {
public:
    Layer() {
        absolute(0, 0).padding(Edge::Horizontal, 8).padding(Edge::Vertical, 4).maxWidth(320);
        setVisible(false);
        m_label = &add<Label>("");
    }
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
        if (isVisible()) visible(false);
        if (forget) m_owner = nullptr;
    }

    Widget* hitTest(float, float) override { return nullptr; }
    void onLayout() override { // keep inside the window, flipping above the pointer at the bottom edge
        const SkSize win = window()->size();
        const float x = std::max(0.0f, std::min(m_bounds.left(), win.width() - m_bounds.width()));
        const float y = m_bounds.bottom() > win.height() ? std::max(0.0f, m_at.y() - 4 - m_bounds.height()) : m_bounds.top();
        if (x != m_bounds.left() || y != m_bounds.top()) absolute(x, y);
    }
    void onDraw(SkCanvas* c) override {
        const auto& st = theme().tooltip;
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(st.background);
        c->drawRRect(SkRRect::MakeRectXY(m_bounds, 4, 4), p);
        p.setStyle(SkPaint::kStroke_Style);
        p.setColor(st.border);
        c->drawRRect(SkRRect::MakeRectXY(m_bounds.makeInset(0.5f, 0.5f), 4, 4), p);
    }

private:
    void show() {
        const auto& st = theme().tooltip;
        m_label->text(m_text).fontSize(st.fontSize).color(st.text);
        absolute(m_at.x(), m_at.y() + 20); // below the cursor
        Widget* root = parent();
        if (root->children().back().get() != this) root->adopt(root->detach(this)); // above everything
        visible(true);
    }

    Label* m_label;
    const Widget* m_owner = nullptr;
    std::string m_text;
    SkPoint m_at{0, 0};
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
