#include "terracotta2/Docking/DockSpace.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include "include/core/SkPaint.h"
#include "terracotta2/Core/Application.hpp"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include "terracotta2/Terracotta2.hpp"

namespace Terracotta2 {

// Yoga treats a flexGrow sum < 1 as "take only that fraction of the free space", so ratios are
// scaled to keep the sum >= 1 (sum is always kGrowScale).
static constexpr float kGrowScale = 100.0f;
static constexpr float kDragThreshold = 4.0f;
static constexpr float kEdgeBand = 0.25f;
static constexpr float kSplitRatio = 0.3f; // share of the new pane when docking to an edge

// ---------------------------------------------------------------- private widgets

class DockTabBar : public Widget {
public:
    DockTabBar() {
        direction(Direction::Row);
        height(theme().dock.tabHeight);
        shrink(0);
    }
    void onThemeChanged() override { height(theme().dock.tabHeight); }
    void onDraw(SkCanvas* canvas) override {
        SkPaint p;
        p.setColor(theme().dock.tabBarBackground);
        canvas->drawRect(m_bounds, p);
    }
};

class DockTabButton : public Widget {
public:
    explicit DockTabButton(std::string title) {
        direction(Direction::Row);
        alignItems(Align::Center);
        padding(Edge::Horizontal, theme().dock.tabPaddingX);
        shrink(0);
        m_label = &add<Label>(std::move(title));
        m_label->color(theme().dock.tabTextInactive);
    }

    void setTitle(std::string t) { m_label->text(std::move(t)); }
    void onThemeChanged() override {
        padding(Edge::Horizontal, theme().dock.tabPaddingX);
        m_label->color(m_active ? theme().dock.tabText : theme().dock.tabTextInactive);
    }
    void setActive(bool a) {
        if (a == m_active) return;
        m_active = a;
        m_label->color(a ? theme().dock.tabText : theme().dock.tabTextInactive);
        markPaintDirty();
    }

    void onDraw(SkCanvas* canvas) override {
        const DockStyle& d = theme().dock;
        SkPaint p;
        p.setColor(m_active ? d.tabActive : m_hover ? d.tabHover : d.tabInactive);
        canvas->drawRect(m_bounds, p);
    }
    void onMouseEnter() override { m_hover = true; markPaintDirty(); }
    void onMouseLeave() override { m_hover = false; markPaintDirty(); }

    bool onMouseDown(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        if (DockNode* n = node()) {
            int i = index(n);
            if (i >= 0) n->setActiveTab(i);
        }
        m_pressed = true;
        m_px = e.x;
        m_py = e.y;
        return true;
    }
    bool onMouseMove(const MouseEvent& e) override {
        if (!m_pressed || std::hypot(e.x - m_px, e.y - m_py) <= kDragThreshold) return m_pressed;
        m_pressed = false;
        DockNode* n = node();
        DockSpace* ds = n ? n->dockSpace() : nullptr;
        int i = n ? index(n) : -1;
        if (!ds || i < 0) return true;
        // From here the global drag owns the pointer; this button may be destroyed: touch nothing after.
        ds->beginTabDrag(n, i, {m_px - m_bounds.left(), m_py - m_bounds.top()});
        return true;
    }
    bool onMouseUp(const MouseEvent&) override {
        bool was = m_pressed;
        m_pressed = false;
        return was;
    }

private:
    DockNode* node() const { return parent() && parent()->parent() ? static_cast<DockNode*>(parent()->parent()) : nullptr; }
    int index(const DockNode* n) const {
        for (size_t i = 0; i < n->m_tabs.size(); ++i)
            if (n->m_tabs[i].button == this) return static_cast<int>(i);
        return -1;
    }
    Label* m_label = nullptr;
    bool m_active = false, m_hover = false, m_pressed = false;
    float m_px = 0, m_py = 0;
};

class DockSplitter : public Widget {
public:
    explicit DockSplitter(bool horizontal) : m_horizontal(horizontal) {
        if (horizontal) width(theme().dock.splitterSize);
        else height(theme().dock.splitterSize);
        shrink(0);
        grow(0);
    }
    Cursor cursor() const override { return m_horizontal ? Cursor::HResize : Cursor::VResize; }
    void onThemeChanged() override {
        if (m_horizontal) width(theme().dock.splitterSize);
        else height(theme().dock.splitterSize);
    }
    void onDraw(SkCanvas* canvas) override {
        SkPaint p;
        p.setColor(m_hover || m_pressed ? theme().dock.splitterHover : theme().dock.splitter);
        canvas->drawRect(m_bounds, p);
    }
    void onMouseEnter() override { m_hover = true; markPaintDirty(); }
    void onMouseLeave() override { m_hover = false; markPaintDirty(); }
    bool onMouseDown(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        m_pressed = true;
        markPaintDirty();
        return true;
    }
    bool onMouseUp(const MouseEvent&) override {
        if (!m_pressed) return false;
        m_pressed = false;
        markPaintDirty();
        return true;
    }
    bool onMouseMove(const MouseEvent& e) override {
        if (!m_pressed) return false;
        auto* p = static_cast<DockNode*>(parent());
        const SkRect& b = p->bounds();
        float sz = theme().dock.splitterSize;
        float avail = (m_horizontal ? b.width() : b.height()) - sz;
        if (avail <= 0) return true;
        float pos = (m_horizontal ? e.x - b.left() : e.y - b.top()) - sz * 0.5f;
        p->setRatio(pos / avail);
        return true;
    }

private:
    bool m_horizontal;
    bool m_hover = false, m_pressed = false;
};

// ---------------------------------------------------------------- DockNode

DockNode::DockNode() {
    direction(Direction::Column);
    grow(1.0f);
    shrink(1.0f);
    basis(0.0f);
    ensureLeafChrome();
}

DockNode::~DockNode() = default;

DockSpace* DockNode::dockSpace() const {
    for (Widget* w = parent(); w; w = w->parent())
        if (auto* d = dynamic_cast<DockSpace*>(w)) return d;
    return nullptr;
}

void DockNode::ensureLeafChrome() {
    if (!isLeaf() || m_tabBar) return;
    m_tabBar = &add<DockTabBar>();
    m_contentArea = &add<Widget>();
    m_contentArea->direction(Direction::Column);
    m_contentArea->grow(1.0f);
    m_contentArea->clipChildren(true);
}

void DockNode::applyActive() {
    for (int i = 0; i < tabCount(); ++i) {
        const Tab& t = m_tabs[i];
        bool a = i == m_active;
        if (t.content) {
            t.content->visible(a);
            if (a) t.content->grow(1.0f);
        }
        static_cast<DockTabButton*>(t.button)->setActive(a);
    }
}

std::pair<DockNode*, DockNode*> DockNode::splitHorizontal(float ratio) { return split(true, ratio, false); }
std::pair<DockNode*, DockNode*> DockNode::splitVertical(float ratio) { return split(false, ratio, false); }

std::pair<DockNode*, DockNode*> DockNode::split(bool horizontal, float ratio, bool existingToSecond) {
    assert(isLeaf());
    if (!isLeaf()) return {nullptr, nullptr};
    ratio = std::clamp(ratio, 0.05f, 0.95f);

    auto a = std::make_unique<DockNode>();
    auto b = std::make_unique<DockNode>();
    DockNode *pa = a.get(), *pb = b.get();
    DockNode* existing = existingToSecond ? pb : pa;

    int active = m_active;
    while (!m_tabs.empty()) {
        std::string title;
        auto content = takeTab(0, &title);
        existing->addTab(std::move(title), std::move(content));
    }
    if (active >= 0) existing->setActiveTab(active);

    clearChildren(); // leaf chrome (tab bar is empty now)
    m_tabBar = m_contentArea = nullptr;
    m_active = -1;

    m_horizontal = horizontal;
    m_ratio = ratio;
    direction(horizontal ? Direction::Row : Direction::Column);
    m_first = pa;
    m_second = pb;
    adopt(std::move(a));
    m_splitter = &add<DockSplitter>(horizontal);
    adopt(std::move(b));
    pa->grow(ratio * kGrowScale);
    pb->grow((1.0f - ratio) * kGrowScale);
    return {pa, pb};
}

void DockNode::setRatio(float ratio) {
    if (isLeaf()) return;
    m_ratio = std::clamp(ratio, 0.05f, 0.95f);
    m_first->grow(m_ratio * kGrowScale);
    m_second->grow((1.0f - m_ratio) * kGrowScale);
}

Widget* DockNode::addTab(std::string title, std::unique_ptr<Widget> content) {
    assert(isLeaf());
    ensureLeafChrome();
    Widget* button = &m_tabBar->add<DockTabButton>(title);
    Widget* c = content ? m_contentArea->adopt(std::move(content)) : nullptr;
    m_tabs.push_back({std::move(title), c, button});
    setActiveTab(tabCount() - 1);
    return c;
}

void DockNode::setActiveTab(int index) {
    if (index < 0 || index >= tabCount()) return;
    m_active = index;
    applyActive();
}

const std::string& DockNode::tabTitle(int index) const { return m_tabs[index].title; }

void DockNode::setTabTitle(std::string title) {
    if (m_tabs.empty()) {
        addTab(std::move(title), nullptr);
        return;
    }
    Tab& t = m_tabs[m_active];
    t.title = title;
    static_cast<DockTabButton*>(t.button)->setTitle(std::move(title));
}

Widget* DockNode::setContent(std::unique_ptr<Widget> content) {
    if (m_tabs.empty()) return addTab("", std::move(content));
    Tab& t = m_tabs[m_active];
    if (t.content) m_contentArea->detach(t.content); // destroyed
    t.content = content ? m_contentArea->adopt(std::move(content)) : nullptr;
    applyActive();
    return t.content;
}

std::unique_ptr<Widget> DockNode::takeTab(int index, std::string* titleOut) {
    if (index < 0 || index >= tabCount()) return nullptr;
    Tab t = std::move(m_tabs[index]);
    m_tabs.erase(m_tabs.begin() + index);
    if (titleOut) *titleOut = std::move(t.title);
    std::unique_ptr<Widget> content = t.content ? m_contentArea->detach(t.content) : nullptr;
    m_tabBar->detach(t.button); // button destroyed here (may be the caller's own mouse-capture widget)

    if (m_tabs.empty()) m_active = -1;
    else if (index < m_active) --m_active;
    else if (index == m_active) m_active = std::min(index, tabCount() - 1);
    applyActive();
    if (content) content->visible(true);
    return content;
}

// ---------------------------------------------------------------- DockSpace

DockSpace::DockSpace() {
    direction(Direction::Column);
    grow(1.0f);
    add<DockNode>();
}

DockNode* DockSpace::rootNode() const { return static_cast<DockNode*>(children().front().get()); }

static DockSpace* findDockSpace(Widget* w) {
    if (auto* d = dynamic_cast<DockSpace*>(w)) return d;
    for (auto& c : w->children())
        if (DockSpace* d = findDockSpace(c.get())) return d;
    return nullptr;
}

DockSpace* DockSpace::of(Window* window) { return window ? findDockSpace(window->root()) : nullptr; }

// ---------------------------------------------------------------- drag session
// One global session. Tab widgets only call beginTabDrag; everything after runs from
// Application's global drag callbacks (screen coords), so no widget has to survive the drag.

struct DockDrag {
    struct State {
        bool active = false;
        DockNode* node = nullptr;       // where the dragged tab lives right now
        int index = -1;
        DockSpace* space = nullptr;     // DockSpace containing `node`
        Window* follow = nullptr;       // floating window that follows the cursor
        SkPoint grab{0, 0};             // cursor offset inside the tab
        DockSpace* previewSpace = nullptr;
        DockNode* dropNode = nullptr;
        DockSpace::DropZone dropZone = DockSpace::DropZone::None;
    };
    static State g;

    static int totalTabs(Widget* w) {
        if (auto* n = dynamic_cast<DockNode*>(w); n && n->isLeaf()) return n->tabCount();
        int c = 0;
        for (auto& ch : w->children()) c += totalTabs(ch.get());
        return c;
    }

    static void clearPreview() {
        if (g.previewSpace) g.previewSpace->setDrop(nullptr, DockSpace::DropZone::None);
        g.previewSpace = nullptr;
        g.dropNode = nullptr;
        g.dropZone = DockSpace::DropZone::None;
    }

    // Window follows the cursor so it stays over the grabbed tab.
    static void followTo(SkPoint screen) {
        SkPoint origin{0, 0}; // tab top-left inside the following window (0,0 until first layout)
        if (g.node && g.index >= 0 && g.index < g.node->tabCount()) {
            const SkRect& b = g.node->m_tabs[g.index].button->bounds();
            origin = {b.left(), b.top()};
        }
        g.follow->setScreenPosition(screen - g.grab - origin);
    }

    // Cursor left every window: the tab becomes a floating window under the cursor.
    static void detach(SkPoint screen) {
        DockNode* src = g.node;
        DockSpace* srcSpace = g.space;
        float w = 0, h = 0;
        if (src->m_contentArea) { w = src->m_contentArea->bounds().width(); h = src->m_contentArea->bounds().height(); }
        w = std::max(w, 300.0f);
        h = std::max(h, 200.0f);
        std::string title;
        auto content = src->takeTab(g.index, &title);
        SkPoint tl = screen - g.grab;
        DockNode* leaf = srcSpace->floatTab(std::move(title), std::move(content), SkRect::MakeXYWH(tl.x(), tl.y(), w, h));
        srcSpace->tidy(src); // may destroy src and (floating) close the emptied source window
        g.node = leaf;
        g.index = 0;
        g.space = leaf->dockSpace();
        g.follow = g.space->window();
    }

    static void move(SkPoint screen) {
        if (!g.active) return;
        Application& app = Application::get();
        if (g.follow) followTo(screen);
        Window* w = app.windowAt(screen, g.follow);
        if (!w && !g.follow) detach(screen);

        DockSpace* ds = w ? DockSpace::of(w) : nullptr;
        DockNode* n = nullptr;
        DockSpace::DropZone z = DockSpace::DropZone::None;
        if (ds) {
            SkPoint l = w->toLocal(screen);
            std::tie(n, z) = ds->dropTarget(l.x(), l.y());
        }
        if (g.previewSpace != ds) clearPreview();
        g.previewSpace = ds;
        g.dropNode = n;
        g.dropZone = z;
        if (ds) ds->setDrop(n, z);
    }

    static void release(SkPoint screen) {
        if (!g.active) return;
        move(screen);
        DockNode* dn = g.dropNode;
        DockSpace* ds = g.previewSpace;
        DockSpace::DropZone dz = g.dropZone;
        DockNode* node = g.node;
        int index = g.index;
        clearPreview();
        g = State{}; // session over before the tree mutates
        if (!ds || !dn || dz == DockSpace::DropZone::None) return; // stays where it is (floating or home)
        Window* tw = ds->window();
        ds->moveTab(node, index, dn, dz);
        if (tw) tw->raise();
    }

    static void cancel() {
        if (!g.active) return;
        clearPreview();
        g = State{};
        Application::get().cancelGlobalDrag();
    }

    static void spaceDestroyed(DockSpace* d) {
        if (!g.active) return;
        if (g.previewSpace == d) g.previewSpace = nullptr;
        if (g.space == d) cancel();
    }
};
DockDrag::State DockDrag::g;

DockSpace::~DockSpace() { DockDrag::spaceDestroyed(this); }

bool DockSpace::isDragging() { return DockDrag::g.active; }

void DockSpace::beginTabDrag(DockNode* source, int tabIndex, SkPoint grabOffset) {
    if (DockDrag::g.active || !source || !source->isLeaf() || tabIndex < 0 || tabIndex >= source->tabCount()) return;
    auto& g = DockDrag::g;
    g = DockDrag::State{};
    g.active = true;
    g.node = source;
    g.index = tabIndex;
    g.space = this;
    g.grab = grabOffset;
    Window* w = window();
    if (w && w->isFloating() && DockDrag::totalTabs(this) == 1) { // lone tab of a floating window: move the window
        g.follow = w;
        w->raise();
    }
    Application::get().beginGlobalDrag(&DockDrag::move, &DockDrag::release);
}

// ponytail: Escape-to-cancel not wired (Window routes keys to the focused widget only; the tab button
// is usually destroyed mid-drag). Add an Application-level key hook if wanted.
void DockSpace::cancelTabDrag() { DockDrag::cancel(); }

// Floating window -> OS close: its tabs go back into the main window's largest leaf.
static void redockAll(Window* win) {
    DockSpace* fs = DockSpace::of(win);
    Window* mw = Application::get().mainWindow();
    DockSpace* ms = mw && mw != win ? DockSpace::of(mw) : nullptr;
    if (!fs || !ms) return;
    DockNode* target = nullptr;
    float best = -1;
    std::function<void(DockNode*)> largest = [&](DockNode* n) {
        if (!n->isLeaf()) { largest(n->first()); largest(n->second()); return; }
        const SkRect& b = n->bounds();
        if (b.width() * b.height() > best) { best = b.width() * b.height(); target = n; }
    };
    largest(ms->rootNode());
    std::function<DockNode*(DockNode*)> firstWithTabs = [&](DockNode* n) -> DockNode* {
        if (n->isLeaf()) return n->tabCount() ? n : nullptr;
        if (DockNode* r = firstWithTabs(n->first())) return r;
        return firstWithTabs(n->second());
    };
    while (DockNode* leaf = firstWithTabs(fs->rootNode())) ms->moveTab(leaf, 0, target, DockSpace::DropZone::Center);
}

DockNode* DockSpace::floatTab(std::string title, std::unique_ptr<Widget> content, SkRect screenFrame) {
    WindowDesc d;
    d.title = title;
    d.width = std::max(1, static_cast<int>(std::lround(screenFrame.width())));
    d.height = std::max(1, static_cast<int>(std::lround(screenFrame.height())));
    d.x = screenFrame.left();
    d.y = screenFrame.top();
    d.floating = true;
    Window* w = Application::get().createWindow(d);
    DockNode* leaf = w->root()->add<DockSpace>().rootNode();
    leaf->addTab(std::move(title), std::move(content));
    w->setOnClose([w] { redockAll(w); });
    return leaf;
}

SkRect DockSpace::previewRect(DockNode* t, DropZone z) const {
    const SkRect& b = t->bounds();
    switch (z) {
        case DropZone::Center: return b;
        case DropZone::Left: return SkRect::MakeLTRB(b.left(), b.top(), b.left() + b.width() * kSplitRatio, b.bottom());
        case DropZone::Right: return SkRect::MakeLTRB(b.right() - b.width() * kSplitRatio, b.top(), b.right(), b.bottom());
        case DropZone::Top: return SkRect::MakeLTRB(b.left(), b.top(), b.right(), b.top() + b.height() * kSplitRatio);
        case DropZone::Bottom: return SkRect::MakeLTRB(b.left(), b.bottom() - b.height() * kSplitRatio, b.right(), b.bottom());
        default: return SkRect::MakeEmpty();
    }
}

void DockSpace::setDrop(DockNode* n, DropZone z) {
    SkRect r = z == DropZone::None ? SkRect::MakeEmpty() : previewRect(n, z);
    if (n == m_dropNode && z == m_dropZone && r == m_preview) return;
    if (!m_preview.isEmpty()) invalidateRect(m_preview);
    m_dropNode = n;
    m_dropZone = z;
    m_preview = r;
    if (!r.isEmpty()) invalidateRect(r);
}

std::pair<DockNode*, DockSpace::DropZone> DockSpace::dropTarget(float x, float y) const {
    Widget* w = const_cast<DockSpace*>(this)->hitTest(x, y);
    DockNode* leaf = nullptr;
    for (; w && w != this; w = w->parent())
        if (auto* n = dynamic_cast<DockNode*>(w); n && n->isLeaf()) { leaf = n; break; }
    if (!leaf) return {nullptr, DropZone::None};

    const SkRect& b = leaf->bounds();
    if (b.width() <= 0 || b.height() <= 0) return {nullptr, DropZone::None};
    struct { float d; DropZone z; } edges[] = {
        {(x - b.left()) / b.width(), DropZone::Left},
        {(b.right() - x) / b.width(), DropZone::Right},
        {(y - b.top()) / b.height(), DropZone::Top},
        {(b.bottom() - y) / b.height(), DropZone::Bottom},
    };
    auto nearest = *std::min_element(std::begin(edges), std::end(edges), [](auto& p, auto& q) { return p.d < q.d; });
    DropZone zone = nearest.d < kEdgeBand ? nearest.z : DropZone::Center;

    if (DockDrag::g.active && leaf == DockDrag::g.node && (zone == DropZone::Center || leaf->tabCount() <= 1))
        return {nullptr, DropZone::None};
    return {leaf, zone};
}

void DockSpace::moveTab(DockNode* source, int tabIndex, DockNode* target, DropZone zone) {
    if (!source || !target || zone == DropZone::None) return;
    assert(source->isLeaf() && target->isLeaf());
    if (!source->isLeaf() || !target->isLeaf()) return;
    if (tabIndex < 0 || tabIndex >= source->tabCount()) return;
    if (source == target && (zone == DropZone::Center || source->tabCount() == 1)) return;

    std::string title;
    auto content = source->takeTab(tabIndex, &title);

    DockNode* dest = target;
    if (zone != DropZone::Center) {
        bool horiz = zone == DropZone::Left || zone == DropZone::Right;
        bool newFirst = zone == DropZone::Left || zone == DropZone::Top;
        auto [f, s] = target->split(horiz, newFirst ? kSplitRatio : 1.0f - kSplitRatio, newFirst);
        dest = newFirst ? f : s;
    }
    dest->addTab(std::move(title), std::move(content));

    tidy(source);
}

// Emptied source: collapse a non-root leaf; an emptied root of a floating window closes the window.
void DockSpace::tidy(DockNode* source) {
    if (!source->isLeaf() || source->tabCount() != 0) return;
    if (dynamic_cast<DockNode*>(source->parent())) { collapse(source); return; }
    Window* w = source->window();
    if (w && w->isFloating()) w->close(); // deferred
}

void DockSpace::collapse(DockNode* emptyLeaf) {
    auto* P = dynamic_cast<DockNode*>(emptyLeaf->parent());
    if (!P) return; // root stays as an empty leaf
    DockNode* S = P->m_first == emptyLeaf ? P->m_second : P->m_first;
    Widget* G = P->parent();
    size_t idx = G->indexOf(P);
    float grow = YGNodeStyleGetFlexGrow(P->yogaNode());
    bool pWasFirst = false;
    auto* gNode = dynamic_cast<DockNode*>(G);
    if (gNode) pWasFirst = gNode->m_first == P;

    auto sOwned = P->detach(S);
    auto pOwned = G->detach(P);
    G->adopt(std::move(sOwned), idx);
    S->grow(grow);
    S->basis(0.0f);
    if (gNode) (pWasFirst ? gNode->m_first : gNode->m_second) = S;
    // pOwned (empty leaf + splitter) destroyed here
}

void DockSpace::onDrawOver(SkCanvas* canvas) {
    if (m_dropZone == DropZone::None || m_preview.isEmpty()) return;
    SkPaint p;
    p.setColor(theme().dock.dropPreview);
    canvas->drawRect(m_preview, p);
    p.setColor(SkColorSetA(theme().dock.dropPreview, 255));
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(1.0f);
    canvas->drawRect(m_preview.makeInset(0.5f, 0.5f), p);
}

// ponytail: no tab reordering within a leaf, no close buttons, no layout serialization

} // namespace Terracotta2
