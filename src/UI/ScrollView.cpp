#include "terracotta2/UI/ScrollView.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include <algorithm>
#include <cmath>

namespace Terracotta2 {

ScrollView::ScrollView() {
    direction(Direction::Column).grow(1.0f).basis(0.0f).shrink(1.0f).clipChildren(true);
    YGNodeStyleSetOverflow(yogaNode(), YGOverflowScroll);
}

float ScrollView::contentHeight() const {
    float bottom = 0.0f;
    for (const auto& c : children()) {
        if (!c->isVisible()) continue;
        if (isManualLayout()) { bottom = std::max(bottom, c->frame().bottom()); continue; }
        YGNodeRef n = c->yogaNode();
        bottom = std::max(bottom, YGNodeLayoutGetTop(n) + YGNodeLayoutGetHeight(n) + YGNodeLayoutGetMargin(n, YGEdgeBottom));
    }
    return bottom + YGNodeLayoutGetPadding(yogaNode(), YGEdgeBottom);
}

float ScrollView::maxScroll() const { return std::max(0.0f, contentHeight() - m_bounds.height()); }

void ScrollView::scrollToShow(const SkRect& r) {
    if (r.top() < m_bounds.top()) setScrollY(m_scrollY - (m_bounds.top() - r.top()));
    else if (r.bottom() > m_bounds.bottom()) setScrollY(m_scrollY + std::min(r.bottom() - m_bounds.bottom(), r.top() - m_bounds.top()));
}

void ScrollView::setScrollY(float y) {
    y = std::clamp(y, 0.0f, maxScroll());
    if (y == m_scrollY) return;
    invalidateRect(thumbRect()); 
    m_scrollY = y;
    invalidateRect(thumbRect()); // the thumb's new position
    relayoutChildren();
}

bool ScrollView::onScroll(float , float dy) {
    const float before = m_scrollY;
    setScrollY(m_scrollY - dy * theme().scroll.wheelSpeed);
    return m_scrollY != before; 
}

SkRect ScrollView::thumbRect(const SkRect& track, float scroll, float content, float view) {
    const auto& st = theme().scroll;
    if (content <= view || view <= 0) return SkRect::MakeEmpty();
    const float length = track.height() - 2 * st.margin;
    const float thumb = std::max(24.0f, length * view / content);
    const float t = std::clamp(scroll / (content - view), 0.0f, 1.0f);
    const float top = track.top() + st.margin + t * (length - thumb);
    return SkRect::MakeXYWH(track.right() - st.margin - st.thickness, top, st.thickness, thumb);
}

void ScrollView::drawThumb(SkCanvas* canvas, const SkRect& thumb, bool active) {
    if (thumb.isEmpty()) return;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(active ? theme().scroll.thumbActive : theme().scroll.thumb);
    canvas->drawRoundRect(thumb, thumb.width() * 0.5f, thumb.width() * 0.5f, p);
}

void ScrollView::reveal(Widget& w, const SkRect& r) {
    for (Widget* p = w.parent(); p; p = p->parent())
        if (auto* sv = dynamic_cast<ScrollView*>(p)) sv->scrollToShow(r);
}

SkRect ScrollView::thumbRect() const { return thumbRect(m_bounds, m_scrollY, contentHeight(), m_bounds.height()); }

Widget* ScrollView::hitTest(float x, float y) {
    if (isVisible() && thumbRect().contains(x, y)) return this;
    return Widget::hitTest(x, y);
}

bool ScrollView::onMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    const SkRect thumb = thumbRect();
    if (!thumb.contains(e.x, e.y)) return false;
    m_dragGrabY = e.y - thumb.top();
    invalidateRect(thumb); 
    return true;          
}

bool ScrollView::onMouseMove(const MouseEvent& e) {
    if (m_dragGrabY < 0) return false;
    const auto& st = theme().scroll;
    const SkRect thumb = thumbRect();
    const float travel = m_bounds.height() - 2 * st.margin - thumb.height();
    if (travel > 0)
        setScrollY((e.y - m_dragGrabY - m_bounds.top() - st.margin) / travel * maxScroll());
    return true;
}

bool ScrollView::onMouseUp(const MouseEvent&) {
    if (m_dragGrabY < 0) return false;
    m_dragGrabY = -1.0f;
    invalidateRect(thumbRect());
    return true;
}

void ScrollView::onDrawOver(SkCanvas* canvas) { drawThumb(canvas, thumbRect(), m_dragGrabY >= 0); }

void ScrollView::onLayout() { setScrollY(m_scrollY); }

} // namespace Terracotta2
