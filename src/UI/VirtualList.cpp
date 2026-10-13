#include "terracotta2/UI/VirtualList.hpp"
#include <algorithm>
#include <cmath>

namespace Terracotta2 {

VirtualList::VirtualList(float rowHeight) : m_rowH(rowHeight) {
    grow(1).basis(0);
    m_content = &add<Widget>();
    m_content->manualLayout();
}

VirtualList& VirtualList::rows(MakeRow make, BindRow bind) {
    m_make = std::move(make);
    m_bind = std::move(bind);
    m_content->clearChildren();
    m_pool.clear();
    m_bound.clear();
    sync();
    return *this;
}

VirtualList& VirtualList::count(int n) {
    m_count = std::max(0, n);
    m_content->height(m_count * m_rowH);
    // contentHeight() is stale until the next layout, so clamp against the new height here
    const float max = std::max(0.0f, m_count * m_rowH - m_bounds.height());
    if (scrollY() > max) setScrollY(max);
    sync(true);
    return *this;
}

void VirtualList::refresh() {
    sync(true);
    for (Widget* r : m_pool)
        if (r->isVisible()) r->markPaintDirty();
}

void VirtualList::scrollToRow(int i) {
    const float y = i * m_rowH;
    if (y < scrollY()) setScrollY(y);
    else if (y + m_rowH > scrollY() + m_bounds.height()) setScrollY(y + m_rowH - m_bounds.height());
    sync();
}

bool VirtualList::onScroll(float dx, float dy) {
    const bool r = ScrollView::onScroll(dx, dy);
    sync();
    return r;
}

bool VirtualList::onMouseMove(const MouseEvent& e) {
    const bool r = ScrollView::onMouseMove(e);
    if (r) sync();
    return r;
}

void VirtualList::onLayout() {
    ScrollView::onLayout();
    sync();
}

// Row i always uses pool slot i % pool size, so a scroll step rebinds one row, not all of them.
void VirtualList::sync(bool rebind) {
    if (!m_make || !m_bind) return;
    const int need = int(std::ceil(m_bounds.height() / m_rowH)) + 1;
    if (int(m_pool.size()) < need) m_bound.assign(need, -1); // slots shift when the pool grows
    while (int(m_pool.size()) < need) m_pool.push_back(&m_make(*m_content));
    const int P = int(m_pool.size());
    if (P == 0) return;
    const int first = int(scrollY() / m_rowH), last = std::min(m_count, first + need);
    std::vector<bool> used(P);
    for (int i = first; i < last; ++i) {
        Widget* row = m_pool[i % P];
        used[i % P] = true;
        if (!row->isVisible()) row->visible(true);
        if (rebind || m_bound[i % P] != i) m_bind(*row, i), m_bound[i % P] = i;
        row->setFrame(SkRect::MakeXYWH(0, i * m_rowH, m_bounds.width(), m_rowH));
    }
    for (int k = 0; k < P; ++k)
        if (!used[k] && m_pool[k]->isVisible()) m_pool[k]->visible(false), m_bound[k] = -1;
}

} // namespace Terracotta2
