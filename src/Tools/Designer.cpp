#include "terracotta2/Tools/Designer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <typeinfo>
#include <vector>
#if defined(__clang__) || defined(__GNUC__)
#include <cxxabi.h>
#endif

#include "terracotta2/Core/Application.hpp"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include "terracotta2/UI/Container.hpp"
#include "terracotta2/UI/Controls.hpp"

namespace Terracotta2 {

namespace {

constexpr float kNameW = 150;

struct State {
    Window* win = nullptr;    // the designer window
    Window* target = nullptr; // window holding the selection (its highlight IS the selection)
    bool picking = false;
    VBox* inspector = nullptr; // rebuilt on every selection change
} g;

bool alive(Window* w) {
    auto ws = Application::get().windows();
    return w && std::find(ws.begin(), ws.end(), w) != ws.end();
}

// Looked up on every use, never cached: the widget may be destroyed at any time
// (Window clears its highlight then).
Widget* selected() { return !g.picking && alive(g.target) ? g.target->highlight() : nullptr; }
template <class T> T* sel() { return dynamic_cast<T*>(selected()); }

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

// ---------------------------------------------------------------- rows

VBox& section(Widget& parent, const std::string& title, bool open) {
    VBox& box = parent.add<VBox>().gap(6);
    box.add<Button>(title, [&box] {
        Widget* body = box.children()[1].get();
        body->visible(!body->isVisible());
    }).alignSelf(YGAlignStretch);
    return box.add<VBox>().gap(4).padding(YGEdgeLeft, 8).visible(open);
}

void heading(Widget& parent, const std::string& text) {
    parent.add<Label>(text).weight(600).color(theme().textMuted).margin(YGEdgeTop, 6);
}

HBox& row(Widget& parent, const std::string& name) {
    HBox& r = parent.add<HBox>().gap(4).alignItems(YGAlignCenter);
    r.add<Label>(name).width(kNameW);
    return r;
}

DragNumber& numberRow(Widget& parent, const std::string& name, std::function<float()> get, std::function<void(float)> set,
                      float min, float max, float perPixel = 0.25f, int decimals = 1) {
    return row(parent, name).add<DragNumber>(std::move(get), std::move(set), min, max, perPixel, decimals).width(120);
}

// Clickable color chip; the color row's picker opens below it.
class Swatch : public Widget {
public:
    explicit Swatch(std::function<SkColor()> get) : m_get(std::move(get)) { size(48, 20); }
    std::function<void()> onPress;
    bool onClick(const MouseEvent&) override { if (onPress) onPress(); return true; }
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

void colorRow(Widget& parent, const std::string& name, std::function<SkColor()> get, std::function<void(SkColor)> set) {
    Swatch& swatch = row(parent, name).add<Swatch>(get);
    ColorPicker& picker = parent.add<ColorPicker>(get, [set, &swatch](SkColor c) { set(c); swatch.markPaintDirty(); })
                              .margin(YGEdgeLeft, kNameW + 4).margin(YGEdgeBottom, 6).visible(false);
    swatch.onPress = [&picker] { picker.visible(!picker.isVisible()); }; // both live and die with `parent`
}

void textRow(Widget& parent, const std::string& name, const std::string& value, std::function<void(const std::string&)> set) {
    row(parent, name).add<InputText>("", std::move(set)).grow(1).setValue(value);
}

// A button showing the current value; a click applies `next` and rebuilds the inspector (to show it).
void rebuildInspector();
void cycleRow(Widget& parent, const std::string& name, const std::string& value, std::function<void(Widget&)> next) {
    row(parent, name).add<Button>(value, [next] {
        if (Widget* s = selected()) next(*s);
        rebuildInspector(); // destroys this button: nothing may follow
    });
}

// ---------------------------------------------------------------- theme fields

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
    };
}

std::string themeAsCode() {
    std::string s = "Theme myTheme() {\n    Theme t;\n";
    char buf[160];
    for (const ThemeField& f : themeFields()) {
        if (f.color)
            std::snprintf(buf, sizeof buf, "    t.%s = SkColorSetARGB(%u, %u, %u, %u);\n", f.code, SkColorGetA(*f.color),
                          SkColorGetR(*f.color), SkColorGetG(*f.color), SkColorGetB(*f.color));
        else
            std::snprintf(buf, sizeof buf, "    t.%s = %gf;\n", f.code, *f.number);
        s += buf;
    }
    return s + "    t.fontFamily = \"" + appTheme().fontFamily + "\";\n    return t;\n}\n";
}

void buildThemeEditor(Widget& page) {
    std::map<std::string, VBox*> sections;
    for (const ThemeField& f : themeFields()) {
        VBox*& sec = sections[f.section];
        if (!sec) sec = &section(page, f.section, sec == nullptr && sections.size() == 1); // first one open
        if (SkColor* c = f.color)
            colorRow(*sec, f.name, [c] { return *c; }, [c](SkColor v) { *c = v; applyTheme(); });
        else if (float* n = f.number)
            numberRow(*sec, f.name, [n] { return *n; }, [n](float v) { *n = v; applyTheme(); }, f.min, f.max);
    }
    textRow(*sections["Global"], "font family", appTheme().fontFamily, [](const std::string& f) {
        appTheme().fontFamily = f;
        applyTheme();
    });

    page.add<HBox>().gap(8).margin(YGEdgeTop, 8).build([](HBox& r) {
        r.add<Button>("Reset to dark theme", [] {
            appTheme() = darkTheme();
            applyTheme(); // the editors re-read their values through onThemeChanged
        });
        r.add<Button>("Copy theme as C++", [] { Application::get().setClipboard(themeAsCode()); });
    });
}

// ---------------------------------------------------------------- inspector

float points(YGValue v) { return v.unit == YGUnitPoint ? v.value : NAN; } // NaN shows "auto"

void select(Widget* w);

// Number row bound to the selection: get/set receive the selected widget (skipped once it is gone).
DragNumber& prop(Widget& parent, const std::string& name, std::function<float(Widget&)> get,
                 std::function<void(Widget&, float)> set, float min, float max, float perPixel = 0.25f, int decimals = 1) {
    return numberRow(parent, name,
                     [get] { Widget* s = selected(); return s ? get(*s) : NAN; },
                     [set](float v) { if (Widget* s = selected()) set(*s, v); }, min, max, perPixel, decimals);
}

// A Yoga dimension: drag (from the current size while it is auto) plus an "auto" reset.
void sizeProp(Widget& parent, const std::string& name, YGValue (*get)(YGNodeConstRef), void (*set)(YGNodeRef, float),
              void (*reset)(YGNodeRef), bool horizontal) {
    prop(parent, name, [get](Widget& s) { return points(get(s.getYGNode())); },
         [set](Widget& s, float v) { set(s.getYGNode(), v); s.markLayoutDirty(); }, 0, 4000)
        .whenAuto([horizontal] { Widget* s = selected(); return s ? (horizontal ? s->getBounds().width() : s->getBounds().height()) : 0.0f; });
    parent.children().back()->add<Button>("auto", [reset] {
        if (Widget* s = selected()) { reset(s->getYGNode()); s->markLayoutDirty(); }
        rebuildInspector();
    });
}

Theme& designerTheme() { // fixed look: the designer must not change with what it edits
    static Theme t = darkTheme();
    return t;
}

void rebuildInspector() {
    ThemeScope scope(&designerTheme()); // also called from the pick hook, i.e. inside another window's scope
    VBox& body = *g.inspector;
    body.clearChildren();
    Widget* w = selected();
    if (!w) {
        body.add<Label>(g.picking ? "Click a widget in another window." : "Nothing selected.").color(theme().textMuted);
        return;
    }
    const SkRect b = w->getBounds();
    char buf[96];
    std::snprintf(buf, sizeof buf, "x %.1f  y %.1f  w %.1f  h %.1f", b.x(), b.y(), b.width(), b.height());
    body.add<Label>(describe(*w)).weight(600);
    body.add<Label>(buf).color(theme().textMuted);
    YGNodeRef n = w->getYGNode();

    // ---- type-specific
    if (auto* l = dynamic_cast<Label*>(w)) {
        heading(body, "Label");
        textRow(body, "text", l->text(), [](const std::string& v) { if (auto* s = sel<Label>()) s->text(v); });
        prop(body, "font size", [](Widget& s) { return static_cast<Label&>(s).fontSize(); },
             [](Widget& s, float v) { static_cast<Label&>(s).fontSize(v); }, 4, 120);
        prop(body, "weight", [](Widget& s) { return float(static_cast<Label&>(s).weight()); },
             [](Widget& s, float v) { static_cast<Label&>(s).weight(int(std::lround(v / 100) * 100)); }, 100, 900, 4, 0);
        prop(body, "line height", [](Widget& s) { return static_cast<Label&>(s).lineHeight(); },
             [](Widget& s, float v) { static_cast<Label&>(s).lineHeight(v); }, 0, 4, 0.01f, 2);
        colorRow(body, "color", [] { auto* s = sel<Label>(); return s ? s->color() : SK_ColorTRANSPARENT; },
                 [](SkColor c) { if (auto* s = sel<Label>()) s->color(c); });
        textRow(body, "font family", l->font(), [](const std::string& v) { if (auto* s = sel<Label>()) s->font(v); });
        cycleRow(body, "selectable", l->isSelectable() ? "yes" : "no",
                 [](Widget& s) { auto& lb = static_cast<Label&>(s); lb.selectable(!lb.isSelectable()); });
    }
    if (dynamic_cast<Panel*>(w)) {
        heading(body, "Panel");
        colorRow(body, "background", [] { auto* s = sel<Panel>(); return s ? s->background() : SK_ColorTRANSPARENT; },
                 [](SkColor c) { if (auto* s = sel<Panel>()) s->background(c); });
    }
    if (auto* in = dynamic_cast<InputText*>(w)) {
        heading(body, "InputText");
        textRow(body, "value", in->value(), [](const std::string& v) { if (auto* s = sel<InputText>()) s->setValue(v); });
    }
    if (dynamic_cast<ScrollView*>(w)) {
        heading(body, "ScrollView");
        prop(body, "scroll y", [](Widget& s) { return static_cast<ScrollView&>(s).scrollY(); },
             [](Widget& s, float v) { static_cast<ScrollView&>(s).setScrollY(v); }, 0, 100000, 1, 0);
    }

    // ---- layout (Yoga)
    heading(body, "Flex");
    cycleRow(body, "direction", YGFlexDirectionToString(YGNodeStyleGetFlexDirection(n)),
             [](Widget& s) { s.direction(YGFlexDirection((YGNodeStyleGetFlexDirection(s.getYGNode()) + 1) % 4)); });
    cycleRow(body, "justify", YGJustifyToString(YGNodeStyleGetJustifyContent(n)),
             [](Widget& s) { s.justify(YGJustify((YGNodeStyleGetJustifyContent(s.getYGNode()) + 1) % 6)); });
    cycleRow(body, "align items", YGAlignToString(YGNodeStyleGetAlignItems(n)),
             [](Widget& s) { s.alignItems(YGAlign((YGNodeStyleGetAlignItems(s.getYGNode()) + 1) % 9)); });
    cycleRow(body, "align self", YGAlignToString(YGNodeStyleGetAlignSelf(n)),
             [](Widget& s) { s.alignSelf(YGAlign((YGNodeStyleGetAlignSelf(s.getYGNode()) + 1) % 9)); });
    cycleRow(body, "wrap", YGWrapToString(YGNodeStyleGetFlexWrap(n)),
             [](Widget& s) { s.wrap(YGWrap((YGNodeStyleGetFlexWrap(s.getYGNode()) + 1) % 3)); });
    prop(body, "grow", [](Widget& s) { return YGNodeStyleGetFlexGrow(s.getYGNode()); }, [](Widget& s, float v) { s.grow(v); }, 0, 10, 0.02f, 2);
    prop(body, "shrink", [](Widget& s) { return YGNodeStyleGetFlexShrink(s.getYGNode()); }, [](Widget& s, float v) { s.shrink(v); }, 0, 10, 0.02f, 2);
    sizeProp(body, "basis", YGNodeStyleGetFlexBasis, YGNodeStyleSetFlexBasis, YGNodeStyleSetFlexBasisAuto, true);

    heading(body, "Size");
    sizeProp(body, "width", YGNodeStyleGetWidth, YGNodeStyleSetWidth, YGNodeStyleSetWidthAuto, true);
    sizeProp(body, "height", YGNodeStyleGetHeight, YGNodeStyleSetHeight, YGNodeStyleSetHeightAuto, false);
    // min/max have no "auto" in Yoga: unset is YGUndefined.
    sizeProp(body, "min width", YGNodeStyleGetMinWidth, YGNodeStyleSetMinWidth, [](YGNodeRef r) { YGNodeStyleSetMinWidth(r, YGUndefined); }, true);
    sizeProp(body, "min height", YGNodeStyleGetMinHeight, YGNodeStyleSetMinHeight, [](YGNodeRef r) { YGNodeStyleSetMinHeight(r, YGUndefined); }, false);
    sizeProp(body, "max width", YGNodeStyleGetMaxWidth, YGNodeStyleSetMaxWidth, [](YGNodeRef r) { YGNodeStyleSetMaxWidth(r, YGUndefined); }, true);
    sizeProp(body, "max height", YGNodeStyleGetMaxHeight, YGNodeStyleSetMaxHeight, [](YGNodeRef r) { YGNodeStyleSetMaxHeight(r, YGUndefined); }, false);

    heading(body, "Spacing");
    prop(body, "gap", [](Widget& s) { float v = YGNodeStyleGetGap(s.getYGNode(), YGGutterAll); return std::isnan(v) ? 0.0f : v; },
         [](Widget& s, float v) { s.gap(v); }, 0, 200);
    const struct { const char* name; YGEdge edge; } edges[] = {
        {"left", YGEdgeLeft}, {"top", YGEdgeTop}, {"right", YGEdgeRight}, {"bottom", YGEdgeBottom}};
    for (auto e : edges)
        prop(body, std::string("padding ") + e.name, [e](Widget& s) { return YGNodeLayoutGetPadding(s.getYGNode(), e.edge); },
             [e](Widget& s, float v) { s.padding(e.edge, v); }, 0, 200);
    for (auto e : edges)
        prop(body, std::string("margin ") + e.name, [e](Widget& s) { return YGNodeLayoutGetMargin(s.getYGNode(), e.edge); },
             [e](Widget& s, float v) { s.margin(e.edge, v); }, -200, 200);

    heading(body, "Position");
    cycleRow(body, "type", YGPositionTypeToString(YGNodeStyleGetPositionType(n)), [](Widget& s) {
        YGPositionType t = YGNodeStyleGetPositionType(s.getYGNode());
        YGNodeStyleSetPositionType(s.getYGNode(), t == YGPositionTypeAbsolute ? YGPositionTypeRelative : YGPositionTypeAbsolute);
        s.markLayoutDirty();
    });
    for (auto e : edges)
        prop(body, e.name, [e](Widget& s) { return points(YGNodeStyleGetPosition(s.getYGNode(), e.edge)); },
             [e](Widget& s, float v) { YGNodeStyleSetPosition(s.getYGNode(), e.edge, v); s.markLayoutDirty(); }, -4000, 4000);

    heading(body, "Widget");
    cycleRow(body, "visible", w->isVisible() ? "yes" : "no", [](Widget& s) { s.visible(!s.isVisible()); });
    cycleRow(body, "clip children", w->clipsChildren() ? "yes" : "no", [](Widget& s) { s.clipChildren(!s.clipsChildren()); s.markPaintDirty(); });

    // ---- tree navigation, relative to the current selection
    heading(body, "Tree");
    if (w->parent())
        body.add<Button>("Parent: " + describe(*w->parent()), [] {
            if (Widget* s = selected(); s && s->parent()) select(s->parent());
        });
    for (size_t i = 0; i < w->children().size(); ++i)
        body.add<Button>("Child: " + describe(*w->children()[i]), [i] {
            if (Widget* s = selected(); s && i < s->children().size()) select(s->children()[i].get());
        });
}

void select(Widget* w) {
    Window* win = w ? w->window() : nullptr;
    if (alive(g.target) && g.target != win) g.target->setHighlight(nullptr);
    g.target = win;
    if (win) win->setHighlight(w);
    rebuildInspector();
}

// Mouse hook while picking: hover outlines the widget under the pointer, a press selects it.
bool pick(Window* win, float x, float y, bool press) {
    if (win == g.win) return false; // the designer itself stays usable
    Widget* hit = win->getRootWidget()->hitTest(x, y);
    if (alive(g.target) && g.target != win) g.target->setHighlight(nullptr);
    g.target = win;
    win->setHighlight(hit);
    if (press) {
        g.picking = false;
        Application::get().setMouseHook(nullptr);
        rebuildInspector();
    }
    return true;
}

} // namespace

Window* openDesigner() {
    if (alive(g.win)) {
        g.win->raise();
        return g.win;
    }
    g = {};
    Application& app = Application::get();
    g.win = app.createWindow({.title = "Terracotta2 Designer", .width = 560, .height = 860});
    g.win->setTheme(&designerTheme());
    ThemeScope scope(&designerTheme());
    g.win->setOnClose([] {
        if (g.picking) Application::get().setMouseHook(nullptr);
        if (alive(g.target)) g.target->setHighlight(nullptr);
        g = {};
    });

    ScrollView& page = g.win->getRootWidget()->add<Panel>().grow(1).add<ScrollView>().padding(12).gap(10);
    VBox& inspect = section(page, "Inspector", true);
    inspect.add<Button>("Pick widget", [] {
        if (alive(g.target)) g.target->setHighlight(nullptr);
        g.picking = true;
        Application::get().setMouseHook(pick);
        rebuildInspector();
    });
    g.inspector = &inspect.add<VBox>().gap(4);
    rebuildInspector();
    buildThemeEditor(page);
    return g.win;
}

} // namespace Terracotta2
