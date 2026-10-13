#include "terracotta2/Tools/Designer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <typeinfo>
#include <vector>
#if defined(__clang__) || defined(__GNUC__)
#include <cxxabi.h>
#endif

#include "include/core/SkPathBuilder.h"
#include "terracotta2/Core/Application.hpp"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include "terracotta2/Terracotta2.hpp"

namespace Terracotta2 {

namespace {

constexpr float kNameW = 96;  // property name column
constexpr float kIndent = 14; // tree depth step

class DesignerPanel;

struct State {
    DesignerPanel* panel = nullptr;
    Window* host = nullptr;    // the window the designer was opened for (the tree shows it when nothing is selected)
    Window* target = nullptr;  // window holding the selection (its highlight IS the selection)
    Window* previewWin = nullptr;
    bool picking = false;
    bool rebuildPosted = false;
    int page = 0;
    std::array<Widget*, 2> pages{}; // Inspector (tree above properties), Theme
    std::array<Widget*, 2> tabs{};
    float treeHeight = 240;         // tree pane, resized with the split handle
    bool revealPending = false;     // scroll the tree to the selection once it is laid out
    Widget* selectedRow = nullptr;  // the tree row of the selection, if built
    VBox* toolbar = nullptr;
    VBox* inspector = nullptr;
    VBox* tree = nullptr;
    std::set<const Widget*> expanded;   // tree rows opened by the user: compared, never dereferenced
    std::map<std::string, bool> closed; // inspector sections the user collapsed
    struct ThemeRow {
        Widget* row;
        Widget* picker; // color rows only
        Widget* section;
        std::string key; // lower case, for the filter
    };
    std::vector<ThemeRow> themeRows;
} g;

Theme& designerTheme() { // fixed look: the designer must not change with what it edits
    static Theme t = darkTheme();
    return t;
}

bool alive(Window* w) {
    auto ws = Application::get().windows();
    return w && std::find(ws.begin(), ws.end(), w) != ws.end();
}

bool inTree(const Widget* root, const Widget* w) {
    if (root == w) return true;
    for (auto& c : root->children())
        if (inTree(c.get(), w)) return true;
    return false;
}

// Looked up on every use, never cached: the widget may be destroyed at any time
// (Window clears its highlight then).
Widget* selected() { return !g.picking && alive(g.target) ? g.target->highlight() : nullptr; }
template <class T> T* sel() { return dynamic_cast<T*>(selected()); }
Window* treeWindow() { return alive(g.target) ? g.target : alive(g.host) ? g.host : nullptr; }

std::string typeName(const Widget& w) {
    std::string s = typeid(w).name();
#if defined(__clang__) || defined(__GNUC__)
    int status = 0;
    if (char* d = abi::__cxa_demangle(s.c_str(), nullptr, nullptr, &status)) {
        s = d;
        std::free(d);
    }
#endif
    if (auto i = s.rfind("::"); i != std::string::npos) s = s.substr(i + 2);
    if (auto i = s.rfind(' '); i != std::string::npos) s = s.substr(i + 1); // MSVC "class X"
    return s;
}

std::string describe(const Widget& w) {
    std::string s = typeName(w);
    if (auto* l = dynamic_cast<const Label*>(&w)) s += " \"" + l->text().substr(0, 24) + "\"";
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The widget the designer shows for `w`: nullptr inside hidden plumbing (dock tab bars, splitters),
// else w or its nearest ancestor that is not skipped (dock nodes and their content areas).
Widget* inspectable(Widget* w) {
    for (Widget* a = w; a; a = a->parent())
        if (a->inspect() == Widget::Inspect::Hide) return nullptr;
    while (w && w->inspect() == Widget::Inspect::Skip) w = w->parent();
    return w;
}

// w's children as the tree shows them: skipped ones replaced by their own, hidden ones left out.
void inspectableChildren(const Widget* w, std::vector<Widget*>& out) {
    for (auto& c : w->children()) {
        if (c->inspect() == Widget::Inspect::Hide || (g.panel && c.get() == reinterpret_cast<Widget*>(g.panel))) continue;
        if (c->inspect() == Widget::Inspect::Skip) inspectableChildren(c.get(), out);
        else out.push_back(c.get());
    }
}

// ---------------------------------------------------------------- selection

void rebuild();
void scheduleRebuild() { // deferred: the caller is often a widget that the rebuild destroys
    if (g.rebuildPosted) return;
    g.rebuildPosted = true;
    Application::get().post([] {
        g.rebuildPosted = false;
        if (g.panel) rebuild();
    });
}

void setPreview(Window* win, Widget* w) {
    if (alive(g.previewWin) && g.previewWin != win) g.previewWin->setPreview(nullptr);
    g.previewWin = win;
    if (win) win->setPreview(w);
}

void select(Widget* w) {
    Window* win = w ? w->window() : nullptr;
    if (alive(g.target) && g.target != win) g.target->setHighlight(nullptr);
    if (alive(g.target) && !win) g.target->setHighlight(nullptr);
    g.target = win;
    if (win) win->setHighlight(w);
    for (Widget* a = w ? w->parent() : nullptr; a; a = a->parent()) g.expanded.insert(a); // reveal it in the tree
    g.revealPending = w != nullptr;
    scheduleRebuild();
}

void stopPicking() {
    if (!g.picking) return;
    g.picking = false;
    Application::get().setMouseHook(nullptr);
    setPreview(nullptr, nullptr);
}

// Mouse hook while picking: hover outlines the widget under the pointer, a press selects it.
bool pick(Window* win, float x, float y, bool press);
void startPicking() {
    if (alive(g.target)) g.target->setHighlight(nullptr);
    g.picking = true;
    Application::get().setMouseHook(pick);
    scheduleRebuild();
}

bool pick(Window* win, float x, float y, bool press) {
    Widget* hit = win->root()->hitTest(x, y);
    if (hit && g.panel && reinterpret_cast<Widget*>(g.panel)->isAncestorOf(hit)) { // the designer stays usable
        setPreview(nullptr, nullptr);
        return false;
    }
    hit = inspectable(hit);
    if (!hit) { // dock chrome and other plumbing: not pickable, keep picking
        setPreview(win, nullptr);
        return true;
    }
    setPreview(win, hit);
    if (press) {
        stopPicking();
        select(hit);
    }
    return true;
}

// ---------------------------------------------------------------- small widgets

// Disclosure triangle: points right when closed, down when open.
class Disclosure : public Widget {
public:
    explicit Disclosure(bool open, std::function<void()> onClick = nullptr) : m_open(open), m_onClick(std::move(onClick)) {
        size(12, 12).shrink(0);
    }
    void setOpen(bool o) { m_open = o; markPaintDirty(); }
    bool onMouseClick(const MouseEvent& e) override {
        if (!m_onClick || e.button != MouseButton::Left) return false; // none: the click goes to the parent
        m_onClick();
        return true;
    }
    void onDraw(SkCanvas* c) override {
        const float cx = m_bounds.centerX(), cy = m_bounds.centerY(), r = 3.5f;
        SkPathBuilder p;
        if (m_open) p.moveTo(cx - r, cy - r * 0.6f).lineTo(cx + r, cy - r * 0.6f).lineTo(cx, cy + r * 0.8f).close();
        else p.moveTo(cx - r * 0.6f, cy - r).lineTo(cx + r * 0.8f, cy).lineTo(cx - r * 0.6f, cy + r).close();
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(theme().textMuted);
        c->drawPath(p.detach(), paint);
    }

private:
    bool m_open;
    std::function<void()> m_onClick;
};

// Collapsible section header; remembers its state by key across rebuilds.
class SectionHeader : public HBox {
public:
    SectionHeader(const std::string& title, std::string key, Widget& body) : m_key(std::move(key)), m_body(body) {
        height(26).alignItems(Align::Center).padding(Edge::Horizontal, 6).gap(6).margin(Edge::Top, 4);
        m_arrow = &add<Disclosure>(body.isVisible());
        add<Label>(title).weight(600);
    }
    bool onMouseClick(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        const bool open = !m_body.isVisible();
        m_body.visible(open);
        m_arrow->setOpen(open);
        g.closed[m_key] = !open;
        return true;
    }
    Cursor cursor() const override { return Cursor::Hand; }
    void onMouseEnter() override { markPaintDirty(); }
    void onMouseLeave() override { markPaintDirty(); }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(isHovered() ? theme().button.hoverBackground : theme().button.background);
        c->drawRoundRect(m_bounds, 4, 4, p);
    }

private:
    std::string m_key;
    Widget& m_body;
    Disclosure* m_arrow;
};

// Tab of the designer's own page strip (Inspector / Tree / Theme).
void showPage(int i);
class PageTab : public Widget {
public:
    PageTab(std::string text, int index) : m_index(index) {
        padding(Edge::Horizontal, 10).height(30).justify(Justify::Center).shrink(0);
        add<Label>(std::move(text));
    }
    bool onMouseClick(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        showPage(m_index);
        return true;
    }
    Cursor cursor() const override { return Cursor::Hand; }
    void onMouseEnter() override { markPaintDirty(); }
    void onMouseLeave() override { markPaintDirty(); }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        if (isHovered()) {
            p.setColor(SkColorSetA(theme().label.text, 0x12));
            c->drawRect(m_bounds, p);
        }
        if (g.page == m_index) {
            p.setColor(theme().accent);
            c->drawRect(SkRect::MakeLTRB(m_bounds.left(), m_bounds.bottom() - 2, m_bounds.right(), m_bounds.bottom()), p);
        }
    }

private:
    int m_index;
};

void showPage(int i) {
    g.page = i;
    for (int k = 0; k < 2; ++k) {
        if (g.pages[k]) g.pages[k]->visible(k == i);
        if (g.tabs[k]) g.tabs[k]->markPaintDirty();
    }
    if (i == 0) scheduleRebuild(); // the tree may be stale
}

// Horizontal bar between the tree and the properties; dragging it resizes the tree pane, like the
// split in browser dev tools.
class SplitHandle : public Widget {
public:
    explicit SplitHandle(Widget& above) : m_above(above) { height(5).shrink(0); }
    Cursor cursor() const override { return Cursor::VResize; }
    bool onMouseDown(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        m_drag = true;
        markPaintDirty();
        return true;
    }
    bool onMouseMove(const MouseEvent& e) override {
        if (!m_drag) return false;
        const float room = parent() ? parent()->bounds().bottom() - m_above.bounds().top() - 120 : 400; // keep some properties visible
        g.treeHeight = std::clamp(e.y - m_above.bounds().top() - 2, 60.0f, std::max(60.0f, room));
        m_above.basis(g.treeHeight);
        return true;
    }
    bool onMouseUp(const MouseEvent&) override {
        m_drag = false;
        markPaintDirty();
        return true;
    }
    void onMouseEnter() override { markPaintDirty(); }
    void onMouseLeave() override { markPaintDirty(); }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setColor(m_drag || isHovered() ? theme().dock.splitterHover : theme().dock.splitter);
        c->drawRect(m_bounds, p);
    }

private:
    Widget& m_above;
    bool m_drag = false;
};

// One widget of the target window in the tree page. Hover previews it, a click selects it.
class TreeRow : public HBox {
public:
    TreeRow(Widget* w, int depth, size_t childCount, bool open) : m_w(w) {
        height(22).alignItems(Align::Center).gap(4).padding(Edge::Left, 6 + depth * kIndent).padding(Edge::Right, 6).shrink(0);
        if (childCount)
            add<Disclosure>(open, [w] {
                if (!g.expanded.erase(w)) g.expanded.insert(w);
                scheduleRebuild();
            });
        else
            add<Widget>().size(12, 12);
        Label& l = add<Label>(describe(*w)).fontSize(12);
        if (!w->isVisible()) l.color(theme().textMuted);
        if (childCount) add<Label>(std::to_string(childCount)).fontSize(11).color(theme().textMuted);
    }
    void onMouseEnter() override {
        if (Window* win = treeWindow(); win && inTree(win->root(), m_w)) setPreview(win, m_w);
        markPaintDirty();
    }
    void onMouseLeave() override {
        setPreview(nullptr, nullptr);
        markPaintDirty();
    }
    bool onMouseClick(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        Window* win = treeWindow();
        if (!win || !inTree(win->root(), m_w)) return scheduleRebuild(), true; // gone: refresh the tree
        if (selected() == m_w) select(nullptr); // a second click deselects
        else select(m_w);
        return true;
    }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        if (selected() == m_w) p.setColor(SkColorSetA(theme().accent, 0x60));
        else if (isHovered()) p.setColor(SkColorSetA(theme().label.text, 0x12));
        else return;
        c->drawRect(m_bounds, p);
    }

private:
    Widget* m_w; // validated against the target window's tree before use
};

// ---------------------------------------------------------------- property rows

// key: remembers open/closed across rebuilds (default: the title).
VBox& section(Widget& parent, const std::string& title, std::string key = {}) {
    if (key.empty()) key = title;
    VBox& box = parent.add<VBox>();
    VBox& b = box.add<VBox>().gap(4).padding(6).visible(!g.closed[key]);
    box.adopt(std::make_unique<SectionHeader>(title, key, b), 0);
    return b;
}

HBox& row(Widget& parent, const std::string& name) {
    HBox& r = parent.add<HBox>().gap(4).alignItems(Align::Center);
    r.add<Label>(name).width(kNameW).shrink(0).color(theme().textMuted);
    return r;
}

DragNumber& numberRow(Widget& parent, const std::string& name, std::function<float()> get, std::function<void(float)> set,
                      float min, float max, float perPixel = 0.25f, int decimals = 1) {
    return row(parent, name).add<DragNumber>(std::move(get), std::move(set), min, max, perPixel, decimals).width(110);
}

// Clickable color chip; the color row's picker opens below it.
class Swatch : public Widget {
public:
    explicit Swatch(std::function<SkColor()> get) : m_get(std::move(get)) { size(48, 20); }
    std::function<void()> onPress;
    bool onMouseClick(const MouseEvent&) override { if (onPress) onPress(); return true; }
    Cursor cursor() const override { return Cursor::Hand; }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(m_get());
        c->drawRoundRect(m_bounds, 3, 3, p);
        p.setStyle(SkPaint::kStroke_Style);
        p.setColor(theme().input.border);
        c->drawRoundRect(m_bounds.makeInset(0.5f, 0.5f), 3, 3, p);
    }

private:
    std::function<SkColor()> m_get;
};

// Returns the row; *pickerOut gets the (hidden) picker below it.
HBox& colorRow(Widget& parent, const std::string& name, std::function<SkColor()> get, std::function<void(SkColor)> set,
               Widget** pickerOut = nullptr) {
    HBox& r = row(parent, name);
    Swatch& swatch = r.add<Swatch>(get);
    ColorPicker& picker = parent.add<ColorPicker>(get, [set, &swatch](SkColor c) { set(c); swatch.markPaintDirty(); })
                              .margin(Edge::Left, kNameW + 4).margin(Edge::Bottom, 6).visible(false);
    swatch.onPress = [&picker] { picker.visible(!picker.isVisible()); }; // both live and die with `parent`
    if (pickerOut) *pickerOut = &picker;
    return r;
}

void textRow(Widget& parent, const std::string& name, const std::string& value, std::function<void(const std::string&)> set) {
    row(parent, name).add<InputText>("").onChange(std::move(set)).grow(1).value(value);
}

// Enum as a dropdown; set receives the selected widget (skipped once it is gone).
void enumRow(Widget& parent, const std::string& name, std::vector<std::string> items, int current,
             std::function<void(Widget&, int)> set) {
    row(parent, name).add<Dropdown>(std::move(items), current).grow(1).onChange([set](int i) {
        if (Widget* s = selected()) set(*s, i);
    });
}

void boolRow(Widget& parent, const std::string& name, bool value, std::function<void(Widget&, bool)> set) {
    row(parent, name).add<Toggle>(value).onChange([set](bool v) {
        if (Widget* s = selected()) set(*s, v);
    });
}

// Number row bound to the selection: get/set receive the selected widget (skipped once it is gone).
DragNumber& prop(Widget& parent, const std::string& name, std::function<float(Widget&)> get,
                 std::function<void(Widget&, float)> set, float min, float max, float perPixel = 0.25f, int decimals = 1) {
    return numberRow(parent, name,
                     [get] { Widget* s = selected(); return s ? get(*s) : NAN; },
                     [set](float v) { if (Widget* s = selected()) set(*s, v); }, min, max, perPixel, decimals);
}

// Left / top / right / bottom in one row.
constexpr Edge kEdges[] = {Edge::Left, Edge::Top, Edge::Right, Edge::Bottom};
void edgesRow(Widget& parent, const std::string& name, std::function<float(Widget&, Edge)> get,
              std::function<void(Widget&, Edge, float)> set, float min, float max) {
    HBox& r = row(parent, name);
    const char* tips[] = {"left", "top", "right", "bottom"};
    for (int i = 0; i < 4; ++i) {
        const Edge e = kEdges[i];
        r.add<DragNumber>([get, e] { Widget* s = selected(); return s ? get(*s, e) : NAN; },
                          [set, e](float v) { if (Widget* s = selected()) set(*s, e, v); }, min, max, 0.25f, 0)
            .grow(1).basis(0).tooltip(tips[i]);
    }
}

float points(YGValue v) { return v.unit == YGUnitPoint ? v.value : NAN; } // NaN shows "auto"

// A Yoga dimension: drag (from the current size while it is auto) plus an "auto" reset.
void sizeProp(Widget& parent, const std::string& name, YGValue (*get)(YGNodeConstRef), void (*set)(YGNodeRef, float),
              void (*reset)(YGNodeRef), bool horizontal) {
    prop(parent, name, [get](Widget& s) { return points(get(s.yogaNode())); },
         [set](Widget& s, float v) { set(s.yogaNode(), v); s.markLayoutDirty(); }, 0, 4000)
        .whenAuto([horizontal] { Widget* s = selected(); return s ? (horizontal ? s->bounds().width() : s->bounds().height()) : 0.0f; });
    parent.children().back()->add<Button>("auto").onClick([reset] {
        if (Widget* s = selected()) { reset(s->yogaNode()); s->markLayoutDirty(); }
        scheduleRebuild();
    });
}

std::vector<std::string> names(const char* (*toString)(int), int count) {
    std::vector<std::string> v;
    for (int i = 0; i < count; ++i) v.emplace_back(toString(i));
    return v;
}

// ---------------------------------------------------------------- selection as C++

#if defined(__clang__) || defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
std::string format(const char* fmt, ...) {
    char b[200];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(b, sizeof b, fmt, args);
    va_end(args);
    return b;
}

std::string colorCode(SkColor c) {
    char b[64];
    std::snprintf(b, sizeof b, "SkColorSetARGB(%u, %u, %u, %u)", SkColorGetA(c), SkColorGetR(c), SkColorGetG(c), SkColorGetB(c));
    return b;
}

// The selection's style as a setter chain, for pasting into the code that builds it. Yoga values
// equal to a fresh node's defaults are left out.
std::string widgetCode(Widget& w) {
    static const YGNodeRef def = YGNodeNew();
    YGNodeRef n = w.yogaNode();
    std::vector<std::string> out;
#define EMIT(...) out.push_back(format(__VA_ARGS__))

    static const char* dirs[] = {"Column", "ColumnReverse", "Row", "RowReverse"};
    static const char* justs[] = {"Start", "Center", "End", "SpaceBetween", "SpaceAround", "SpaceEvenly"};
    static const char* aligns[] = {"Auto", "Start", "Center", "End", "Stretch", "Baseline"}; // Align covers YGAlign 0..5
    auto align = [&](const char* fn, YGAlign v) {
        if (v <= YGAlignBaseline) EMIT(".%s(Align::%s)", fn, aligns[v]);
        else EMIT("// %s: %s (no Align value; set it on yogaNode())", fn, YGAlignToString(v));
    };
    if (YGNodeStyleGetFlexDirection(n) != YGNodeStyleGetFlexDirection(def)) EMIT(".direction(Direction::%s)", dirs[YGNodeStyleGetFlexDirection(n)]);
    if (YGNodeStyleGetJustifyContent(n) != YGNodeStyleGetJustifyContent(def)) EMIT(".justify(Justify::%s)", justs[YGNodeStyleGetJustifyContent(n)]);
    if (YGNodeStyleGetAlignItems(n) != YGNodeStyleGetAlignItems(def)) align("alignItems", YGNodeStyleGetAlignItems(n));
    if (YGNodeStyleGetAlignSelf(n) != YGNodeStyleGetAlignSelf(def)) align("alignSelf", YGNodeStyleGetAlignSelf(n));
    if (YGNodeStyleGetFlexWrap(n) == YGWrapWrap) EMIT(".wrap()");
    if (float v = YGNodeStyleGetFlexGrow(n); v != YGNodeStyleGetFlexGrow(def)) EMIT(".grow(%g)", v);
    if (float v = YGNodeStyleGetFlexShrink(n); v != YGNodeStyleGetFlexShrink(def)) EMIT(".shrink(%g)", v);
    if (YGValue v = YGNodeStyleGetFlexBasis(n); v.unit == YGUnitPoint) EMIT(".basis(%g)", v.value);

    auto dim = [&](const char* fn, YGValue v) {
        if (v.unit == YGUnitPoint) EMIT(".%s(%g)", fn, v.value);
        else if (v.unit == YGUnitPercent) EMIT(".%sPercent(%g)", fn, v.value);
    };
    dim("width", YGNodeStyleGetWidth(n));
    dim("height", YGNodeStyleGetHeight(n));
    const float minW = points(YGNodeStyleGetMinWidth(n)), minH = points(YGNodeStyleGetMinHeight(n));
    if (!std::isnan(minW) || !std::isnan(minH)) {
        auto num = [](float v) { char t[32]; std::snprintf(t, sizeof t, "%g", v); return std::isnan(v) ? std::string("YGUndefined") : std::string(t); };
        out.push_back(".minSize(" + num(minW) + ", " + num(minH) + ")");
    }
    if (float v = points(YGNodeStyleGetMaxWidth(n)); !std::isnan(v)) EMIT(".maxWidth(%g)", v);
    if (float v = points(YGNodeStyleGetMaxHeight(n)); !std::isnan(v)) EMIT(".maxHeight(%g)", v);
    if (float v = YGNodeStyleGetGap(n, YGGutterAll); !std::isnan(v) && v != 0) EMIT(".gap(%g)", v);

    const struct { YGEdge e; const char* name; } edges[] = {{YGEdgeAll, nullptr}, {YGEdgeHorizontal, "Horizontal"}, {YGEdgeVertical, "Vertical"},
                                                            {YGEdgeLeft, "Left"}, {YGEdgeTop, "Top"}, {YGEdgeRight, "Right"}, {YGEdgeBottom, "Bottom"}};
    for (const char* fn : {"padding", "margin"})
        for (auto e : edges) {
            YGValue v = fn[0] == 'p' ? YGNodeStyleGetPadding(n, e.e) : YGNodeStyleGetMargin(n, e.e);
            if (v.unit != YGUnitPoint) continue;
            if (e.name) EMIT(".%s(Edge::%s, %g)", fn, e.name, v.value);
            else EMIT(".%s(%g)", fn, v.value);
        }
    if (YGNodeStyleGetPositionType(n) == YGPositionTypeAbsolute) {
        const float l = points(YGNodeStyleGetPosition(n, YGEdgeLeft)), t = points(YGNodeStyleGetPosition(n, YGEdgeTop));
        EMIT(".absolute(%g, %g)", std::isnan(l) ? 0.0f : l, std::isnan(t) ? 0.0f : t);
    }
    if (!w.isVisible()) EMIT(".visible(false)");
    if (!w.clipsChildren()) EMIT(".clipChildren(false)");

    const Theme& t = appTheme();
    if (auto* l = dynamic_cast<Label*>(&w)) {
        if (l->fontSize() != t.label.fontSize) EMIT(".fontSize(%g)", l->fontSize());
        if (l->weight() != 400) EMIT(".weight(%d)", l->weight());
        if (l->color() != t.label.text) out.push_back(".color(" + colorCode(l->color()) + ")");
        if (!l->font().empty()) out.push_back(".font(\"" + l->font() + "\")");
        if (l->lineHeight() != 0) EMIT(".lineHeight(%g)", l->lineHeight());
    }
    if (auto* p = dynamic_cast<Panel*>(&w); p && p->background() != t.panelBackground)
        out.push_back(".background(" + colorCode(p->background()) + ")");

    std::string s = "// " + describe(w) + "\n";
    if (out.empty()) return s + "// (all defaults)\n";
    for (size_t i = 0; i < out.size(); ++i) s += out[i] + (i + 1 == out.size() ? ";\n" : "\n");
    return s;
}
#undef EMIT

// ---------------------------------------------------------------- pages

void buildToolbar() {
    VBox& bar = *g.toolbar;
    bar.clearChildren();
    bar.add<HBox>().gap(6).build([](HBox& r) {
        if (g.picking) r.add<Button>("Cancel pick").onClick([] { stopPicking(); scheduleRebuild(); });
        else r.add<Button>("Pick").onClick(startPicking).tooltip("Then click a widget in any window");
        if (Widget* s = selected()) {
            if (inspectable(s->parent())) r.add<Button>("Parent").onClick([] { if (Widget* x = selected()) select(inspectable(x->parent())); });
            r.add<Button>("Deselect").onClick([] { select(nullptr); }).tooltip("Esc");
        }
        r.add<Widget>().grow(1);
        r.add<Button>("Refresh").onClick(scheduleRebuild).tooltip("Re-read the widget tree");
    });
}

void buildInspector() {
    VBox& body = *g.inspector;
    body.clearChildren();
    Widget* w = selected();
    if (!w) {
        body.add<Label>(g.picking ? "Click a widget in any window. Esc cancels." : "Nothing selected. Pick a widget, or choose one in the tree above.")
            .color(theme().textMuted);
        return;
    }
    const SkRect b = w->bounds();
    char buf[96];
    std::snprintf(buf, sizeof buf, "x %.1f  y %.1f  w %.1f  h %.1f", b.x(), b.y(), b.width(), b.height());
    body.add<HBox>().gap(8).alignItems(Align::Center).build([w](HBox& r) {
        r.add<Label>(describe(*w)).weight(600).grow(1).shrink(1);
        r.add<Button>("Copy C++").onClick([] {
            if (Widget* s = selected()) Application::get().setClipboard(widgetCode(*s));
        }).tooltip("Its style as a setter chain");
    });
    body.add<Label>(buf).color(theme().textMuted).fontSize(12);
    YGNodeRef n = w->yogaNode();

    // ---- type-specific
    if (auto* l = dynamic_cast<Label*>(w)) {
        VBox& s = section(body, "Label");
        textRow(s, "text", l->text(), [](const std::string& v) { if (auto* x = sel<Label>()) x->text(v); });
        prop(s, "font size", [](Widget& x) { return static_cast<Label&>(x).fontSize(); },
             [](Widget& x, float v) { static_cast<Label&>(x).fontSize(v); }, 4, 120);
        prop(s, "weight", [](Widget& x) { return float(static_cast<Label&>(x).weight()); },
             [](Widget& x, float v) { static_cast<Label&>(x).weight(int(std::lround(v / 100) * 100)); }, 100, 900, 4, 0);
        prop(s, "line height", [](Widget& x) { return static_cast<Label&>(x).lineHeight(); },
             [](Widget& x, float v) { static_cast<Label&>(x).lineHeight(v); }, 0, 4, 0.01f, 2);
        colorRow(s, "color", [] { auto* x = sel<Label>(); return x ? x->color() : SK_ColorTRANSPARENT; },
                 [](SkColor c) { if (auto* x = sel<Label>()) x->color(c); });
        textRow(s, "font family", l->font(), [](const std::string& v) { if (auto* x = sel<Label>()) x->font(v); });
        boolRow(s, "selectable", l->isSelectable(), [](Widget& x, bool v) { static_cast<Label&>(x).selectable(v); });
    }
    if (dynamic_cast<Panel*>(w)) {
        VBox& s = section(body, "Panel");
        colorRow(s, "background", [] { auto* x = sel<Panel>(); return x ? x->background() : SK_ColorTRANSPARENT; },
                 [](SkColor c) { if (auto* x = sel<Panel>()) x->background(c); });
    }
    if (auto* in = dynamic_cast<InputText*>(w)) {
        VBox& s = section(body, "InputText");
        textRow(s, "value", in->value(), [](const std::string& v) { if (auto* x = sel<InputText>()) x->value(v); });
    }
    if (dynamic_cast<ScrollView*>(w)) {
        VBox& s = section(body, "ScrollView");
        prop(s, "scroll y", [](Widget& x) { return static_cast<ScrollView&>(x).scrollY(); },
             [](Widget& x, float v) { static_cast<ScrollView&>(x).setScrollY(v); }, 0, 100000, 1, 0);
    }

    // ---- layout (Yoga)
    VBox& flex = section(body, "Flex");
    enumRow(flex, "direction", names([](int i) { return YGFlexDirectionToString(YGFlexDirection(i)); }, 4),
            YGNodeStyleGetFlexDirection(n), [](Widget& x, int i) { x.direction(Direction(i)); });
    enumRow(flex, "justify", names([](int i) { return YGJustifyToString(YGJustify(i)); }, 6), YGNodeStyleGetJustifyContent(n),
            [](Widget& x, int i) { x.justify(Justify(i)); });
    enumRow(flex, "align items", names([](int i) { return YGAlignToString(YGAlign(i)); }, 9), YGNodeStyleGetAlignItems(n),
            [](Widget& x, int i) { x.alignItems(Align(i)); });
    enumRow(flex, "align self", names([](int i) { return YGAlignToString(YGAlign(i)); }, 9), YGNodeStyleGetAlignSelf(n),
            [](Widget& x, int i) { x.alignSelf(Align(i)); });
    enumRow(flex, "wrap", names([](int i) { return YGWrapToString(YGWrap(i)); }, 3), YGNodeStyleGetFlexWrap(n), [](Widget& x, int i) {
        YGNodeStyleSetFlexWrap(x.yogaNode(), YGWrap(i));
        x.markLayoutDirty();
    });
    prop(flex, "grow", [](Widget& x) { return YGNodeStyleGetFlexGrow(x.yogaNode()); }, [](Widget& x, float v) { x.grow(v); }, 0, 10, 0.02f, 2);
    prop(flex, "shrink", [](Widget& x) { return YGNodeStyleGetFlexShrink(x.yogaNode()); }, [](Widget& x, float v) { x.shrink(v); }, 0, 10, 0.02f, 2);
    sizeProp(flex, "basis", YGNodeStyleGetFlexBasis, YGNodeStyleSetFlexBasis, YGNodeStyleSetFlexBasisAuto, true);
    prop(flex, "gap", [](Widget& x) { float v = YGNodeStyleGetGap(x.yogaNode(), YGGutterAll); return std::isnan(v) ? 0.0f : v; },
         [](Widget& x, float v) { x.gap(v); }, 0, 200);

    VBox& size = section(body, "Size");
    sizeProp(size, "width", YGNodeStyleGetWidth, YGNodeStyleSetWidth, YGNodeStyleSetWidthAuto, true);
    sizeProp(size, "height", YGNodeStyleGetHeight, YGNodeStyleSetHeight, YGNodeStyleSetHeightAuto, false);
    // min/max have no "auto" in Yoga: unset is YGUndefined.
    sizeProp(size, "min width", YGNodeStyleGetMinWidth, YGNodeStyleSetMinWidth, [](YGNodeRef r) { YGNodeStyleSetMinWidth(r, YGUndefined); }, true);
    sizeProp(size, "min height", YGNodeStyleGetMinHeight, YGNodeStyleSetMinHeight, [](YGNodeRef r) { YGNodeStyleSetMinHeight(r, YGUndefined); }, false);
    sizeProp(size, "max width", YGNodeStyleGetMaxWidth, YGNodeStyleSetMaxWidth, [](YGNodeRef r) { YGNodeStyleSetMaxWidth(r, YGUndefined); }, true);
    sizeProp(size, "max height", YGNodeStyleGetMaxHeight, YGNodeStyleSetMaxHeight, [](YGNodeRef r) { YGNodeStyleSetMaxHeight(r, YGUndefined); }, false);

    VBox& spacing = section(body, "Spacing");
    spacing.add<HBox>().gap(4).build([](HBox& r) { // column captions for the edge rows
        r.add<Widget>().width(kNameW).shrink(0);
        for (const char* c : {"left", "top", "right", "bottom"}) r.add<Label>(c).fontSize(11).color(theme().textMuted).grow(1).basis(0);
    });
    edgesRow(spacing, "padding", [](Widget& x, Edge e) { return YGNodeLayoutGetPadding(x.yogaNode(), YGEdge(e)); },
             [](Widget& x, Edge e, float v) { x.padding(e, v); }, 0, 200);
    edgesRow(spacing, "margin", [](Widget& x, Edge e) { return YGNodeLayoutGetMargin(x.yogaNode(), YGEdge(e)); },
             [](Widget& x, Edge e, float v) { x.margin(e, v); }, -200, 200);

    VBox& pos = section(body, "Position");
    enumRow(pos, "type", names([](int i) { return YGPositionTypeToString(YGPositionType(i)); }, 3), YGNodeStyleGetPositionType(n),
            [](Widget& x, int i) {
                YGNodeStyleSetPositionType(x.yogaNode(), YGPositionType(i));
                x.markLayoutDirty();
            });
    edgesRow(pos, "offset", [](Widget& x, Edge e) { return points(YGNodeStyleGetPosition(x.yogaNode(), YGEdge(e))); },
             [](Widget& x, Edge e, float v) { YGNodeStyleSetPosition(x.yogaNode(), YGEdge(e), v); x.markLayoutDirty(); }, -4000, 4000);

    VBox& misc = section(body, "Widget");
    boolRow(misc, "visible", w->isVisible(), [](Widget& x, bool v) { x.visible(v); });
    boolRow(misc, "clip children", w->clipsChildren(), [](Widget& x, bool v) { x.clipChildren(v); x.markPaintDirty(); });
}

void buildTreeRows(VBox& list, Widget* w, int depth) {
    std::vector<Widget*> kids;
    inspectableChildren(w, kids);
    const bool open = depth == 0 || g.expanded.count(w);
    Widget& r = list.add<TreeRow>(w, depth, kids.size(), open);
    if (w == selected()) g.selectedRow = &r;
    if (open)
        for (Widget* c : kids) buildTreeRows(list, c, depth + 1);
}

void buildTree() {
    VBox& list = *g.tree;
    list.clearChildren();
    g.selectedRow = nullptr;
    Window* win = treeWindow();
    if (!win) {
        list.add<Label>("No window.").color(theme().textMuted);
        return;
    }
    buildTreeRows(list, win->root(), 0);
}

void rebuild() {
    ThemeScope scope(&designerTheme()); // posted: runs outside every window's scope
    buildToolbar();
    buildInspector();
    buildTree();
    if (g.revealPending && g.selectedRow) { // scroll it into view once laid out: posted from a post runs after the next frame
        g.revealPending = false;
        Application::get().post([] {
            Application::get().post([] {
                if (g.selectedRow) ScrollView::reveal(*g.selectedRow, g.selectedRow->bounds());
            });
        });
    }
}

// ---------------------------------------------------------------- theme page

// One table drives both the editor and "Copy theme as C++".
struct ThemeField {
    const char* section;
    const char* name;
    const char* code; // member path inside Theme
    SkColor* color = nullptr;
    float* number = nullptr;
    float min = 0, max = 0;
};

std::vector<ThemeField> themeFields() {
    Theme& t = appTheme();
    auto C = [](const char* s, const char* n, const char* code, SkColor& c) { return ThemeField{s, n, code, &c}; };
    auto N = [](const char* s, const char* n, const char* code, float& f, float lo, float hi) {
        return ThemeField{s, n, code, nullptr, &f, lo, hi};
    };
    return {
        C("Global", "background", "globalBackground", t.globalBackground),
        C("Global", "panel", "panelBackground", t.panelBackground),
        C("Global", "accent", "accent", t.accent),
        C("Global", "track", "track", t.track),
        C("Global", "text muted", "textMuted", t.textMuted),
        C("Global", "selection", "selection", t.selection),
        C("Label", "text", "label.text", t.label.text),
        N("Label", "font size", "label.fontSize", t.label.fontSize, 6, 48),
        C("Button", "background", "button.background", t.button.background),
        C("Button", "hover", "button.hoverBackground", t.button.hoverBackground),
        C("Button", "active", "button.activeBackground", t.button.activeBackground),
        C("Button", "text", "button.text", t.button.text),
        N("Button", "corner radius", "button.cornerRadius", t.button.cornerRadius, 0, 30),
        N("Button", "padding x", "button.paddingX", t.button.paddingX, 0, 60),
        N("Button", "padding y", "button.paddingY", t.button.paddingY, 0, 40),
        N("Button", "hover fade (s)", "button.hoverFadeSec", t.button.hoverFadeSec, 0, 2),
        C("Input", "background", "input.background", t.input.background),
        C("Input", "border", "input.border", t.input.border),
        C("Input", "focus border", "input.focusBorder", t.input.focusBorder),
        C("Input", "text", "input.text", t.input.text),
        C("Input", "placeholder", "input.placeholder", t.input.placeholder),
        C("Input", "caret", "input.caret", t.input.caret),
        N("Input", "corner radius", "input.cornerRadius", t.input.cornerRadius, 0, 30),
        N("Input", "padding x", "input.paddingX", t.input.paddingX, 0, 60),
        N("Input", "height", "input.height", t.input.height, 12, 80),
        C("Dock", "tab bar", "dock.tabBarBackground", t.dock.tabBarBackground),
        C("Dock", "tab active", "dock.tabActive", t.dock.tabActive),
        C("Dock", "tab inactive", "dock.tabInactive", t.dock.tabInactive),
        C("Dock", "tab hover", "dock.tabHover", t.dock.tabHover),
        C("Dock", "tab text", "dock.tabText", t.dock.tabText),
        C("Dock", "tab text inactive", "dock.tabTextInactive", t.dock.tabTextInactive),
        C("Dock", "splitter", "dock.splitter", t.dock.splitter),
        C("Dock", "splitter hover", "dock.splitterHover", t.dock.splitterHover),
        C("Dock", "drop preview", "dock.dropPreview", t.dock.dropPreview),
        N("Dock", "tab height", "dock.tabHeight", t.dock.tabHeight, 12, 80),
        N("Dock", "tab padding x", "dock.tabPaddingX", t.dock.tabPaddingX, 0, 60),
        N("Dock", "splitter size", "dock.splitterSize", t.dock.splitterSize, 1, 30),
        C("Scroll", "thumb", "scroll.thumb", t.scroll.thumb),
        C("Scroll", "thumb active", "scroll.thumbActive", t.scroll.thumbActive),
        N("Scroll", "thickness", "scroll.thickness", t.scroll.thickness, 1, 30),
        N("Scroll", "margin", "scroll.margin", t.scroll.margin, 0, 20),
        N("Scroll", "wheel speed", "scroll.wheelSpeed", t.scroll.wheelSpeed, 0.1f, 5),
        C("Popup", "background", "popup.background", t.popup.background),
        C("Popup", "border", "popup.border", t.popup.border),
        C("Popup", "item hover", "popup.itemHover", t.popup.itemHover),
        N("Popup", "corner radius", "popup.cornerRadius", t.popup.cornerRadius, 0, 30),
        N("Popup", "item height", "popup.itemHeight", t.popup.itemHeight, 12, 60),
        C("Tooltip", "background", "tooltip.background", t.tooltip.background),
        C("Tooltip", "border", "tooltip.border", t.tooltip.border),
        C("Tooltip", "text", "tooltip.text", t.tooltip.text),
        N("Tooltip", "font size", "tooltip.fontSize", t.tooltip.fontSize, 6, 32),
        N("Tooltip", "delay (s)", "tooltip.delaySec", t.tooltip.delaySec, 0, 3),
        C("Title bar", "background", "titleBar.background", t.titleBar.background),
        C("Title bar", "button icon", "titleBar.buttonIcon", t.titleBar.buttonIcon),
        C("Title bar", "close hover", "titleBar.closeHover", t.titleBar.closeHover),
        C("Title bar", "close pressed", "titleBar.closePressed", t.titleBar.closePressed),
    };
}

std::string themeAsCode() {
    std::string s = "Theme myTheme() {\n    Theme t;\n";
    char buf[160];
    for (const ThemeField& f : themeFields()) {
        if (f.color)
            std::snprintf(buf, sizeof buf, "    t.%s = %s;\n", f.code, colorCode(*f.color).c_str());
        else
            std::snprintf(buf, sizeof buf, "    t.%s = %gf;\n", f.code, *f.number);
        s += buf;
    }
    return s + "    t.fontFamily = \"" + appTheme().fontFamily + "\";\n    return t;\n}\n";
}

// Shows the theme rows whose section, name or code contain `filter` (all when empty).
void filterTheme(const std::string& filter) {
    const std::string f = lower(filter);
    std::map<Widget*, bool> any;
    for (auto& r : g.themeRows) {
        const bool match = f.empty() || r.key.find(f) != std::string::npos;
        r.row->visible(match);
        if (!match && r.picker) r.picker->visible(false);
        any[r.section] |= match;
    }
    for (auto [body, shown] : any) {
        body->parent()->visible(shown); // the section (header + body)
        if (!f.empty() && shown) body->visible(true);
    }
}

void buildThemePage(Widget& page) {
    page.add<InputText>("Filter, e.g. button or radius").onChange(filterTheme).margin(Edge::Bottom, 4);
    ScrollView& list = page.add<ScrollView>().gap(2).padding(Edge::Right, 8);
    std::map<std::string, VBox*> sections;
    for (const ThemeField& f : themeFields()) {
        VBox*& sec = sections[f.section];
        if (!sec) {
            const std::string key = std::string("theme/") + f.section; // apart from the inspector's sections
            if (!g.closed.count(key)) g.closed[key] = sections.size() > 1; // first one open
            sec = &section(list, f.section, key);
        }
        Widget* picker = nullptr;
        Widget* r;
        if (SkColor* c = f.color)
            r = &colorRow(*sec, f.name, [c] { return *c; }, [c](SkColor v) { *c = v; applyTheme(); }, &picker);
        else
            r = numberRow(*sec, f.name, [n = f.number] { return *n; }, [n = f.number](float v) { *n = v; applyTheme(); }, f.min, f.max).parent();
        g.themeRows.push_back({r, picker, sec, lower(std::string(f.section) + " " + f.name + " " + f.code)});
    }
    textRow(*sections["Global"], "font family", appTheme().fontFamily, [](const std::string& f) {
        appTheme().fontFamily = f;
        applyTheme();
    });
    g.themeRows.push_back({sections["Global"]->children().back().get(), nullptr, sections["Global"], "global font family fontfamily"});

    page.add<HBox>().gap(8).margin(Edge::Top, 8).build([](HBox& r) {
        r.add<Button>("Reset to dark theme").onClick([] {
            appTheme() = darkTheme();
            applyTheme(); // the editors re-read their values through onThemeChanged
        });
        r.add<Button>("Copy theme as C++").onClick([] { Application::get().setClipboard(themeAsCode()); });
    });
}

// ---------------------------------------------------------------- panel

class DesignerPanel : public Panel {
public:
    DesignerPanel() {
        setTheme(&designerTheme());
        ThemeScope scope(&designerTheme());
        grow(1);
        g.panel = this;

        add<HBox>().alignItems(Align::Center).padding(Edge::Horizontal, 6).shrink(0).build([](HBox& head) {
            const char* titles[] = {"Inspector", "Theme"};
            for (int i = 0; i < 2; ++i) g.tabs[i] = &head.add<PageTab>(titles[i], i);
            head.add<Widget>().grow(1);
            head.add<Button>("Close").onClick([] { closeDesigner(); });
        });

        // Inspector: toolbar, then the widget tree above the selection's properties, split like
        // browser dev tools.
        VBox& inspect = add<VBox>().grow(1).basis(0);
        g.toolbar = &inspect.add<VBox>().padding(8).shrink(0);
        ScrollView& tree = inspect.add<ScrollView>().basis(g.treeHeight).grow(0).shrink(0).padding(Edge::Vertical, 4); // basis: ScrollView's is 0
        g.tree = &tree.add<VBox>();
        inspect.add<SplitHandle>(tree);
        g.inspector = &inspect.add<ScrollView>().padding(10).add<VBox>().gap(4);
        g.pages[0] = &inspect;

        VBox& themePage = add<VBox>().grow(1).basis(0).padding(10).gap(6);
        buildThemePage(themePage);
        g.pages[1] = &themePage;

        showPage(g.page);
        rebuild();
    }
    ~DesignerPanel() override {
        stopPicking();
        setPreview(nullptr, nullptr);
        if (alive(g.target)) g.target->setHighlight(nullptr);
        g = {};
    }
    bool onKey(const KeyEvent& e) override { // reaches here from any focused widget inside
        if (e.key != Key::Escape || e.action != KeyAction::Press) return false;
        if (g.picking) stopPicking();
        else select(nullptr);
        scheduleRebuild();
        return true;
    }
};

// Moves a window's UI (all but its title bar and overlays) into the central node of a new DockSpace,
// so tabs can dock beside it while it stays as it was.
DockSpace& wrapInDockSpace(Window& win) {
    Widget* root = win.root();
    auto content = std::make_unique<Widget>();
    content->direction(Direction::Column).inspect(Widget::Inspect::Skip);
    size_t at = SIZE_MAX;
    for (size_t i = 0; i < root->children().size();) {
        Widget* c = root->children()[i].get();
        if (dynamic_cast<TitleBar*>(c) || YGNodeStyleGetPositionType(c->yogaNode()) == YGPositionTypeAbsolute) {
            ++i;
            continue;
        }
        if (at == SIZE_MAX) at = i;
        content->adopt(root->detach(c));
    }
    DockSpace& ds = static_cast<DockSpace&>(*root->adopt(std::make_unique<DockSpace>(), at));
    ds.customFloatingTitlebar(win.hasCustomTitlebar());
    ds.setCentral(std::move(content));
    return ds;
}

DockNode* rightmostLeaf(DockNode* n) {
    while (!n->isLeaf()) n = n->isHorizontal() ? n->second() : n->first();
    return n;
}

} // namespace

Widget* openDesigner(Window* host) {
    if (g.panel) {
        DockSpace::revealTab(g.panel);
        return g.panel;
    }
    Application& app = Application::get();
    if (!host) host = app.mainWindow();
    if (!host) return nullptr;
    DockSpace* ds = DockSpace::of(host);
    if (!ds) ds = &wrapInDockSpace(*host);
    DockNode* target = ds->centralNode();
    if (!target) target = rightmostLeaf(ds->rootNode());
    const float width = target->bounds().width() > 0 ? target->bounds().width() : host->size().width(); // just wrapped: not laid out yet
    const float ratio = std::clamp(380.0f / std::max(1.0f, width), 0.2f, 0.5f);
    auto panel = std::make_unique<DesignerPanel>();
    Widget* p = panel.get();
    g.host = host;
    ds->dock("Designer", std::move(panel), target, DockSpace::DropZone::Right, ratio);
    return p;
}

void closeDesigner() {
    if (g.panel) DockSpace::closeTab(g.panel);
}

} // namespace Terracotta2
