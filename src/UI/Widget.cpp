#include "terracotta2/UI/Widget.hpp"
#include <algorithm>
#include <cassert>
#include "terracotta2/Core/Window.hpp"

namespace Terracotta2 {

Widget::Widget() : m_yg_node(YGNodeNew()) { YGNodeSetContext(m_yg_node, this); }

Widget::~Widget() {
    // Only base fields are touched: derived parts of ancestors may already be gone.
    if (Window* w = window()) w->forgetSubtree(this);
    m_children.clear(); // children free their Yoga nodes while ours is still alive
    YGNodeFree(m_yg_node);
}

// ---- tree ----

namespace {
void registerAnimations(Widget* w, Window* win) {
    if (w->isAnimating()) win->startAnimating(w);
    for (auto& c : w->children()) registerAnimations(c.get(), win);
}
} // namespace

Widget* Widget::adopt(std::unique_ptr<Widget> child, size_t index) {
    Widget* c = child.get();
    index = std::min(index, m_children.size());
    if (!m_manual) YGNodeInsertChild(m_yg_node, c->m_yg_node, index);
    c->m_parent = this;
    m_children.insert(m_children.begin() + static_cast<std::ptrdiff_t>(index), std::move(child));
    markLayoutDirty();
    if (Window* w = window()) registerAnimations(c, w); // tweens started while detached
    return c;
}

std::unique_ptr<Widget> Widget::detach(Widget* child) {
    auto it = std::find_if(m_children.begin(), m_children.end(),
                           [child](const auto& p) { return p.get() == child; });
    if (it == m_children.end()) return nullptr;
    if (Window* w = window()) w->forgetSubtree(child);
    invalidateRect(child->m_bounds);
    if (!m_manual) YGNodeRemoveChild(m_yg_node, child->m_yg_node);
    child->m_parent = nullptr;
    std::unique_ptr<Widget> owned = std::move(*it);
    m_children.erase(it);
    markLayoutDirty();
    return owned;
}

void Widget::clearChildren() {
    while (!m_children.empty()) detach(m_children.back().get());
}

size_t Widget::indexOf(const Widget* child) const {
    for (size_t i = 0; i < m_children.size(); ++i)
        if (m_children[i].get() == child) return i;
    return SIZE_MAX;
}

Window* Widget::window() const {
    const Widget* w = this;
    while (w->m_parent) w = w->m_parent;
    return w->m_host;
}

bool Widget::isAncestorOf(const Widget* w) const {
    for (; w; w = w->m_parent)
        if (w == this) return true;
    return false;
}

// ---- invalidation ----

void Widget::markLayoutDirty() {
    // No early-out: flags on unvisited subtrees can go stale. Yoga's own dirty
    // propagation is done by its style setters; MarkDirty is only legal on measure nodes.
    for (Widget* w = this; w; w = w->m_parent) {
        w->m_layout_dirty = true;
        if (w == this && YGNodeHasMeasureFunc(w->m_yg_node)) YGNodeMarkDirty(w->m_yg_node);
    }
}

void Widget::markPaintDirty() {
    if (m_visible) invalidateRect(m_bounds);
}

void Widget::invalidateRect(const SkRect& r) {
    if (r.isEmpty()) return;
    SkRect w = r;
    Widget* root = this;
    for (; root->m_parent; root = root->m_parent) {
        const Widget* p = root->m_parent;
        if (!p->m_space) continue;
        const SkPoint a = p->fromChildSpace({w.left(), w.top()}), b = p->fromChildSpace({w.right(), w.bottom()});
        w = SkRect::MakeLTRB(a.x(), a.y(), b.x(), b.y());
        if (!w.intersect(p->m_bounds)) return; // children of a transformed widget are clipped to it
    }
    if (root->m_host) root->m_host->addDamage(w);
}

// ---- style ----

void Widget::setVisible(bool visible) {
    m_visible = visible;
    YGNodeStyleSetDisplay(m_yg_node, visible ? YGDisplayFlex : YGDisplayNone);
    invalidateRect(m_bounds);
    markLayoutDirty();
}

// ---- manual layout ----

void Widget::setManualLayout(bool on) {
    if (on == m_manual) return;
    m_manual = on;
    // Manual: children are separate Yoga roots (laid out inside their frames); otherwise flex items.
    for (size_t i = 0; i < m_children.size(); ++i) {
        YGNodeRef n = m_children[i]->m_yg_node;
        if (on) YGNodeRemoveChild(m_yg_node, n);
        else YGNodeInsertChild(m_yg_node, n, i);
        m_children[i]->markLayoutDirty();
    }
    relayoutChildren();
}

void Widget::setFrame(const SkRect& r) {
    assert(m_parent && m_parent->m_manual && "setFrame: parent is not in manual-layout mode");
    if (!m_parent || !m_parent->m_manual || r == m_frame) return;
    const bool resized = r.width() != m_frame.width() || r.height() != m_frame.height();
    m_frame = r;
    if (resized) markLayoutDirty();            // Yoga re-runs for this subtree only
    else m_parent->relayoutChildren();         // pure move: re-walk, no Yoga
}

// ---- child transform ----

void Widget::setChildTransform(float scale, SkPoint origin) {
    if (m_space && scale == m_scale && origin == m_origin) return;
    const bool first = !m_space;
    m_space = true;
    m_scale = scale;
    m_origin = origin;
    if (first) relayoutChildren(); // children's bounds move into the new space
    markPaintDirty();
}

SkPoint Widget::mapToWindow(SkPoint p) const {
    for (const Widget* a = m_parent; a; a = a->m_parent)
        if (a->m_space) p = a->fromChildSpace(p);
    return p;
}

SkRect Widget::mapToWindow(const SkRect& r) const {
    const SkPoint a = mapToWindow(SkPoint{r.left(), r.top()}), b = mapToWindow(SkPoint{r.right(), r.bottom()});
    return SkRect::MakeLTRB(a.x(), a.y(), b.x(), b.y());
}

SkPoint Widget::mapFromWindow(SkPoint p) const {
    if (!m_parent) return p;
    p = m_parent->mapFromWindow(p); // now in the parent's own space
    return m_parent->m_space ? m_parent->toChildSpace(p) : p;
}

// ---- animation ----

void Widget::animate(float durationSec, std::function<void(float)> onUpdate, std::function<void()> onComplete,
                     Ease ease) {
    m_anim.start(durationSec, std::move(onUpdate), std::move(onComplete), ease);
    if (Window* w = window()) w->startAnimating(this);
}

// ---- input ----

Widget* Widget::hitTest(float x, float y) {
    if (!m_visible || !m_bounds.contains(x, y)) return nullptr;
    const SkPoint c = m_space ? toChildSpace({x, y}) : SkPoint{x, y};
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
        if (Widget* h = (*it)->hitTest(c.x(), c.y())) return h;
    return this;
}

// ---- painting ----

void Widget::paint(SkCanvas* canvas, const SkRect& damage, int& count) {
    if (!m_visible || !SkRect::Intersects(m_bounds.makeOutset(kPaintOverflow, kPaintOverflow), damage)) return;
    onDraw(canvas);
    ++count;
    if (m_clip_children) {
        canvas->save();
        canvas->clipRect(m_bounds);
    }
    if (m_space) {
        canvas->save();
        canvas->translate(m_bounds.left(), m_bounds.top());
        canvas->scale(m_scale, m_scale);
        canvas->translate(-m_origin.x(), -m_origin.y());
        const SkPoint a = toChildSpace({damage.left(), damage.top()}), b = toChildSpace({damage.right(), damage.bottom()});
        const SkRect local = SkRect::MakeLTRB(a.x(), a.y(), b.x(), b.y());
        for (auto& c : m_children) c->paint(canvas, local, count);
        canvas->restore();
    } else {
        for (auto& c : m_children) c->paint(canvas, damage, count);
    }
    if (m_clip_children) canvas->restore();
    onDrawOver(canvas);
}

// ---- layout ----

void Widget::relayoutChildren() {
    m_force_child_layout = true;
    markLayoutDirty(); // bubbles to the root so a frame runs; Yoga itself stays cached
}

void Widget::applyLayout(float parentX, float parentY) {
    const float x = parentX + YGNodeLayoutGetLeft(m_yg_node);
    const float y = parentY + YGNodeLayoutGetTop(m_yg_node);
    const SkRect nb = SkRect::MakeXYWH(x, y, YGNodeLayoutGetWidth(m_yg_node), YGNodeLayoutGetHeight(m_yg_node));
    // A moved subtree keeps identical relative layout, so hasNewLayout alone isn't enough.
    const bool moved = nb.fLeft != m_bounds.fLeft || nb.fTop != m_bounds.fTop;
    // m_layout_dirty is set on every ancestor of a relayoutChildren() caller (bubbling), which
    // is what lets the walk reach it when Yoga itself reports nothing new.
    bool recurse = YGNodeGetHasNewLayout(m_yg_node) || moved || m_force_child_layout || m_layout_dirty;

    if (nb != m_bounds) {
        invalidateRect(m_bounds);
        invalidateRect(nb);
        m_bounds = nb;
        onLayout(); // manual parents place children here (setFrame)
        recurse = recurse || m_force_child_layout || m_layout_dirty; // onLayout may have flagged work
    }
    if (recurse) {
        SkPoint off = childOffset(); // after onLayout(): it may re-clamp the offset
        if (!m_space) off += SkVector{x, y}; // a child space starts at 0, 0
        for (auto& c : m_children) {
            if (m_manual) {
                if (!c->m_visible) continue;
                // Each child is its own Yoga root sized by its frame; only dirty ones recompute.
                if (c->m_layout_dirty)
                    YGNodeCalculateLayout(c->m_yg_node, c->m_frame.width(), c->m_frame.height(), YGDirectionLTR);
                c->applyLayout(off.fX + c->m_frame.fLeft, off.fY + c->m_frame.fTop);
            } else {
                c->applyLayout(off.fX, off.fY);
            }
        }
    }
    m_force_child_layout = false;
    YGNodeSetHasNewLayout(m_yg_node, false);
    m_layout_dirty = false;
}

bool Widget::isHovered() const {
    Window* w = window();
    Widget* h = w ? w->hoveredWidget() : nullptr;
    return h && isAncestorOf(h);
}

} // namespace Terracotta2
