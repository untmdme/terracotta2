// Node editor: a Blender-style node graph that drives a keyframed 2D scene. Four docked panels:
//
//   Nodes (every node, virtualized) | Node Editor  (2000 nodes, pan, select, wire, copy/paste) | Preview
//                                   | Timeline     (ruler, play/scrub, one keyframe track per node) |
//
// Each group of Shape, Position, Size and Color nodes feeds an Output node, which draws one shape in
// the preview. Parameters can be keyframed (diamond buttons, K, or editing an animated value), and the
// timeline moves, deletes and scrubs those keys.
//
// It doubles as a realistic benchmark: run with --bench to drive the UI through scripted interactions
// (playback, panning, dragging, scrolling, resizing a split, copy/paste) and print frame timings.
//   Node_Editor                       interactive
//   Node_Editor --bench               scripted run, prints a table, exits
//   Node_Editor --bench --interactive scripted run, then keeps the window open
//
// Editor: drag = move, Shift+click = add to selection, drag on empty space = box select, scroll or
// middle/Alt-drag = pan, right-click = add/duplicate/delete menu, drag from an output port onto an
// input port = wire (drag a wired input away to disconnect). Keys (any panel): Ctrl/Cmd+C/X/V/D/A,
// Delete, K (key selected nodes), F (frame selection), Space (play), Left/Right (frame), Home/End.
#include <terracotta2/Terracotta2.hpp>
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <numbers>
#include <thread>

using namespace Terracotta2;

namespace NodeEd {

constexpr SkColor kText = SkColorSetRGB(225, 225, 228);
constexpr SkColor kMuted = SkColorSetRGB(140, 140, 146);
constexpr SkColor kSelect = SkColorSetRGB(255, 160, 40);
constexpr SkColor kKey = SkColorSetRGB(230, 200, 70);
constexpr SkColor kPlayhead = SkColorSetRGB(80, 140, 240);
constexpr SkColor kPanel = SkColorSetRGB(34, 34, 36);
constexpr SkColor kCanvas = SkColorSetRGB(26, 26, 28);
constexpr SkColor kRowA = SkColorSetRGB(30, 30, 32);
constexpr SkColor kRowB = SkColorSetRGB(34, 34, 37);
constexpr SkColor kRowSel = SkColorSetRGB(50, 62, 92);

// Node geometry (logical px). Rows: [output] [inputs...] [colour swatch] [params...]
constexpr float kNodeW = 210, kTitleH = 26, kRowH = 24, kFoot = 8, kPortInset = 8, kPortR = 5;
// Timeline / list geometry.
constexpr float kHeaderW = 170, kTrackH = 22, kListRowH = 22, kLanePad = 10;

int firstParamRow(NodeType t) {
    const TypeDef& d = typeDef(t);
    return (d.hasOutput ? 1 : 0) + int(d.inputs.size()) + (t == NodeType::Color ? 1 : 0);
}
float nodeHeight(NodeType t) { return kTitleH + (firstParamRow(t) + typeDef(t).params.size()) * kRowH + kFoot; }
SkRect nodeRect(const Node& n) { return SkRect::MakeXYWH(n.pos.x(), n.pos.y(), kNodeW, nodeHeight(n.type)); }
// Port centres relative to the node's top-left.
SkPoint outputPort() { return {kNodeW - kPortInset, kTitleH + kRowH / 2}; }
SkPoint inputPort(const Node& n, int i) {
    return {kPortInset, kTitleH + ((typeDef(n.type).hasOutput ? 1 : 0) + i + 0.5f) * kRowH};
}

// Time <-> x inside a timeline lane (the ruler and every track share the same horizontal span).
float laneX(const SkRect& lane, float t) { return lane.left() + kLanePad + t / kDuration * (lane.width() - 2 * kLanePad); }
float laneT(const SkRect& lane, float x) {
    return std::clamp((x - lane.left() - kLanePad) / std::max(1.0f, lane.width() - 2 * kLanePad) * kDuration, 0.0f, kDuration);
}

void drawDiamond(SkCanvas* c, SkPoint m, float r, SkColor color, bool filled) {
    SkPathBuilder b;
    b.moveTo(m.x(), m.y() - r);
    b.lineTo(m.x() + r, m.y());
    b.lineTo(m.x(), m.y() + r);
    b.lineTo(m.x() - r, m.y());
    b.close();
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    if (!filled) {
        p.setStyle(SkPaint::kStroke_Style);
        p.setStrokeWidth(1.2f);
    }
    c->drawPath(b.detach(), p);
}

// Output port a -> input port b (window coords), culled against `clip`.
void drawWire(SkCanvas* c, SkPaint& p, SkPoint a, SkPoint b, const SkRect& clip, float zoom) {
    const float dx = std::max(40.0f * zoom, std::abs(b.x() - a.x()) * 0.5f);
    const SkRect hull = SkRect::MakeLTRB(std::min(a.x(), b.x() - dx), std::min(a.y(), b.y()),
                                         std::max(b.x(), a.x() + dx), std::max(a.y(), b.y())).makeOutset(3, 3);
    if (!SkRect::Intersects(hull, clip)) return;
    SkPathBuilder path;
    path.moveTo(a);
    path.cubicTo(a.x() + dx, a.y(), b.x() - dx, b.y(), b.x(), b.y());
    c->drawPath(path.detach(), p);
}

class NodeEditor;
class NodeList;
class TimelinePanel;
class Preview;
class StatusBar;

// The document plus the four panels that show it. Panels report edits here; Studio updates the model
// and tells every panel what to repaint.
struct Studio {
    Doc doc;
    float time = 0;
    bool playing = false;
    Clip clipboard;
    std::vector<KeyRef> selectedKeys; // timeline key selection
    NodeEditor* editor = nullptr;
    NodeList* list = nullptr;
    TimelinePanel* timeline = nullptr;
    Preview* preview = nullptr;
    StatusBar* status = nullptr;

    void setTime(float t, bool fromPlayback = false);
    void togglePlay();
    void editParam(Node* n, int p, float v); // writes a key at the current frame if the parameter is animated
    void toggleKey(Node* n, int p);
    void insertKeys(); // every parameter of the selected nodes, at the current frame
    void deleteSelectedKeys();
    void keysChanged(const std::vector<Node*>& touched = {}); // empty = any node
    bool keySelected(const Node* n, float t) const {
        return std::any_of(selectedKeys.begin(), selectedKeys.end(),
                           [&](const KeyRef& k) { return k.first == n && std::abs(k.second - t) < 0.5f / kFps; });
    }
    void select(Node* n, int mods, bool frame); // click semantics: Shift toggles, otherwise only n
    void selectionChanged();
    void graphChanged(); // wires
    void structureChanged(); // nodes added / removed
    void addNode(NodeType t, SkPoint pos);
    void deleteSelected();
    void paste(const Clip& c, SkVector offset);
    bool handleKey(const KeyEvent& e, Widget& from); // shortcuts shared by every panel
};

// ---------------------------------------------------------------- small controls

// Keyframe toggle for one parameter: filled = key on this frame, outline = animated, grey = static.
class KeyButton : public Widget {
public:
    KeyButton(Studio& s, Node* n, int p) : m_s(s), m_node(n), m_p(p) { size(14, 14).shrink(0); }
    bool onMouseDown(const MouseEvent& e) override { return e.button == MouseButton::Left; }
    bool onMouseClick(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        m_s.toggleKey(m_node, m_p);
        return true;
    }
    Cursor cursor() const override { return Cursor::Hand; }
    void onDraw(SkCanvas* c) override {
        const Param& prm = m_node->params[m_p];
        drawDiamond(c, {m_bounds.centerX(), m_bounds.centerY()}, 5, prm.animated() ? kKey : SkColorSetRGB(90, 90, 96),
                    prm.keyAt(m_s.time) != nullptr);
    }

private:
    Studio& m_s;
    Node* m_node;
    int m_p;
};

// Colour node preview; click for a colour picker that writes Hue/Saturation/Value/Alpha.
class Swatch : public Widget {
public:
    Swatch(Studio& s, Node* n) : m_s(s), m_node(n) { height(18).grow(1).basis(0); }
    SkColor color() const {
        float hsv[3] = {std::fmod(m_node->at(kHue, m_s.time), 360.0f), m_node->at(kSat, m_s.time), m_node->at(kVal, m_s.time)};
        return SkHSVToColor(uint8_t(std::clamp(m_node->at(kAlpha, m_s.time), 0.0f, 1.0f) * 255), hsv);
    }
    bool onMouseDown(const MouseEvent& e) override { return e.button == MouseButton::Left; }
    bool onMouseClick(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        const SkRect b = mapToWindow(m_bounds); // inside the zoomed editor
        auto& box = Overlay::of(*this).open<PopupBox>(b.makeOutset(0, 2)); // below the swatch, or above it near the screen bottom
        box.padding(8);
        box.add<ColorPicker>([this] { return color(); }, [this](SkColor c) {
            float hsv[3];
            SkColorToHSV(c, hsv);
            m_s.editParam(m_node, kHue, hsv[0]);
            m_s.editParam(m_node, kSat, hsv[1]);
            m_s.editParam(m_node, kVal, hsv[2]);
            m_s.editParam(m_node, kAlpha, SkColorGetA(c) / 255.0f);
        });
        return true;
    }
    Cursor cursor() const override { return Cursor::Hand; }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(color());
        c->drawRoundRect(m_bounds, 4, 4, p);
        p.setStyle(SkPaint::kStroke_Style);
        p.setColor(SkColorSetRGB(20, 20, 22));
        c->drawRoundRect(m_bounds.makeInset(0.5f, 0.5f), 4, 4, p);
    }

private:
    Studio& m_s;
    Node* m_node;
};

// Text button that stays pressed while on.
class ToggleButton : public HBox {
public:
    ToggleButton(std::string text, bool on, std::function<void(bool)> onChange) : m_on(on), m_onChange(std::move(onChange)) {
        const auto& st = theme().button; // same metrics as Button
        padding(Edge::Horizontal, st.paddingX).padding(Edge::Vertical, st.paddingY).alignItems(Align::Center);
        add<Label>(std::move(text)).color(st.text);
    }
    bool on() const { return m_on; }
    bool onMouseDown(const MouseEvent& e) override { return e.button == MouseButton::Left; }
    bool onMouseClick(const MouseEvent&) override {
        m_on = !m_on;
        markPaintDirty();
        m_onChange(m_on);
        return true;
    }
    void onMouseEnter() override { markPaintDirty(); }
    void onMouseLeave() override { markPaintDirty(); }
    Cursor cursor() const override { return Cursor::Hand; }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(m_on ? theme().button.activeBackground : isHovered() ? theme().button.hoverBackground : theme().button.background);
        c->drawRoundRect(m_bounds, theme().button.cornerRadius, theme().button.cornerRadius, p);
    }

private:
    bool m_on;
    std::function<void(bool)> m_onChange;
};

class ToolButton : public Widget {
public:
    enum Icon { Start, Play, Pause, End };
    ToolButton(Icon icon, std::function<void()> fn) : m_icon(icon), m_fn(std::move(fn)) { size(30, 24); }
    void setIcon(Icon i) {
        if (i != m_icon) m_icon = i, markPaintDirty();
    }
    bool onMouseDown(const MouseEvent& e) override { return e.button == MouseButton::Left; }
    bool onMouseClick(const MouseEvent&) override {
        m_fn();
        return true;
    }
    void onMouseEnter() override { markPaintDirty(); }
    void onMouseLeave() override { markPaintDirty(); }
    Cursor cursor() const override { return Cursor::Hand; }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(isHovered() ? SkColorSetRGB(64, 64, 70) : SkColorSetRGB(48, 48, 52));
        c->drawRoundRect(m_bounds, 4, 4, p);
        p.setColor(kText);
        const float x = m_bounds.centerX(), y = m_bounds.centerY();
        auto tri = [&](float x0, float dir) { // dir 1 = pointing right
            SkPathBuilder b;
            b.moveTo(x0, y - 5);
            b.lineTo(x0 + 8 * dir, y);
            b.lineTo(x0, y + 5);
            b.close();
            c->drawPath(b.detach(), p);
        };
        switch (m_icon) {
        case Play: tri(x - 3, 1); break;
        case Pause:
            c->drawRect(SkRect::MakeXYWH(x - 5, y - 5, 3.5f, 10), p);
            c->drawRect(SkRect::MakeXYWH(x + 1.5f, y - 5, 3.5f, 10), p);
            break;
        case Start: c->drawRect(SkRect::MakeXYWH(x - 6, y - 5, 2, 10), p); tri(x + 4, -1); break;
        case End: tri(x - 4, 1); c->drawRect(SkRect::MakeXYWH(x + 4, y - 5, 2, 10), p); break;
        }
    }

private:
    Icon m_icon;
    std::function<void()> m_fn;
};

// ---------------------------------------------------------------- node editor

// One node: an ordinary VBox of rows (Yoga lays out the inside); the editor decides where it sits.
class NodeView : public VBox {
public:
    NodeView(Studio& s, Node* n) : m_s(s), m_node(n) {
        const TypeDef& d = typeDef(n->type);
        padding(Edge::Bottom, kFoot);
        add<HBox>().height(kTitleH).alignItems(Align::Center).padding(Edge::Horizontal, 10).build([&](HBox& t) {
            t.add<Label>(n->name).fontSize(12).weight(600).color(SK_ColorWHITE);
        });
        auto row = [&]() -> HBox& {
            return add<HBox>().height(kRowH).alignItems(Align::Center).padding(Edge::Horizontal, 16).gap(6);
        };
        if (d.hasOutput) row().justify(Justify::End).build([&](HBox& r) { r.add<Label>(d.name).fontSize(11).color(kMuted); });
        for (Port p : d.inputs) row().build([&](HBox& r) { r.add<Label>(typeDef(NodeType(int(p))).name).fontSize(11).color(kMuted); });
        if (n->type == NodeType::Color)
            row().build([&](HBox& r) {
                r.add<Label>("Color").fontSize(11).color(kMuted).width(58);
                m_swatch = &r.add<Swatch>(s, n);
            });
        for (int p = 0; p < int(d.params.size()); ++p) {
            const ParamDef& pd = d.params[p];
            row().build([&](HBox& r) {
                r.add<Label>(pd.name).fontSize(11).color(kMuted).width(58);
                Field f;
                auto get = [this, p] { return m_node->at(p, m_s.time); };
                if (pd.choices.empty())
                    f.number = &r.add<DragNumber>(get, [this, p](float v) { m_s.editParam(m_node, p, v); }, pd.min, pd.max,
                                                  pd.perPixel, pd.decimals)
                                    .height(20).grow(1).basis(0);
                else
                    f.choice = &r.add<Dropdown>(std::vector<std::string>(pd.choices.begin(), pd.choices.end()), int(get()))
                                    .onChange([this, p](int i) { m_s.editParam(m_node, p, float(i)); })
                                    .height(20).grow(1).basis(0);
                f.key = &r.add<KeyButton>(s, n, p);
                m_fields.push_back(f);
            });
        }
    }
    Node* node() const { return m_node; }
    DragNumber* numberField(int p) const { return m_fields[p].number; }

    // Re-read parameter values after the time or keys changed. Hidden (off-screen) views are marked
    // stale instead and refresh when they scroll into view.
    bool stale = false;
    void refreshParam(int p) {
        const Field& f = m_fields[p];
        if (f.number) f.number->refresh();
        else f.choice->value(int(m_node->at(p, m_s.time)));
        f.key->markPaintDirty();
        if (m_swatch) m_swatch->markPaintDirty();
    }
    void refresh(bool all) {
        for (int p = 0; p < int(m_fields.size()); ++p)
            if (all || m_node->params[p].animated()) refreshParam(p);
        stale = false;
    }

    void onDraw(SkCanvas* c) override {
        const TypeDef& d = typeDef(m_node->type);
        SkPaint p;
        p.setAntiAlias(true);
        const SkRRect body = SkRRect::MakeRectXY(m_bounds, 6, 6);
        p.setColor(SkColorSetARGB(245, 46, 46, 50));
        c->drawRRect(body, p);
        c->save();
        c->clipRRect(body, true);
        p.setColor(d.color);
        c->drawRect(SkRect::MakeXYWH(m_bounds.left(), m_bounds.top(), m_bounds.width(), kTitleH), p);
        c->restore();
        p.setStyle(SkPaint::kStroke_Style);
        p.setStrokeWidth(m_node->selected ? 2.0f : 1.0f);
        p.setColor(m_node->selected ? kSelect : SkColorSetRGB(16, 16, 18));
        c->drawRRect(SkRRect::MakeRectXY(m_bounds.makeInset(1, 1), 6, 6), p);
        // Ports stay inside the node's bounds, so moving a node never leaves paint behind.
        auto port = [&](SkPoint at, Port type, bool filled) {
            const SkPoint m{m_bounds.left() + at.x(), m_bounds.top() + at.y()};
            p.setStyle(SkPaint::kFill_Style);
            p.setColor(SkColorSetRGB(16, 16, 18));
            c->drawCircle(m, kPortR + 1.5f, p);
            p.setColor(portColor(type));
            if (!filled) {
                p.setStyle(SkPaint::kStroke_Style);
                p.setStrokeWidth(1.5f);
            }
            c->drawCircle(m, filled ? kPortR : kPortR - 1, p);
        };
        if (d.hasOutput) port(outputPort(), Port(int(m_node->type)), true);
        for (int i = 0; i < int(d.inputs.size()); ++i) port(inputPort(*m_node, i), d.inputs[i], m_node->inputs[i] != nullptr);
    }

private:
    struct Field {
        DragNumber* number = nullptr;
        Dropdown* choice = nullptr;
        KeyButton* key = nullptr;
    };
    Studio& m_s;
    Node* m_node;
    std::vector<Field> m_fields;
    Swatch* m_swatch = nullptr;
};

// The graph canvas. Manual layout: each NodeView's frame is its node's position in content space, and
// the view (pan + zoom) is a childTransform, so panning and zooming never re-run layout. Views outside
// the viewport are hidden, so Yoga, painting and hit testing only see the nodes on screen. Zoomed out
// past kLodZoom every view is hidden and the editor draws simple boxes instead (thousands of tiny
// widgets would cost far more than they show). A minimap sits in the bottom-right corner.
constexpr float kMinZoom = 0.04f, kMaxZoom = 2.0f, kLodZoom = 0.35f;
constexpr float kMiniW = 200, kMiniMaxH = 150, kMiniMargin = 10;

class NodeEditor : public Widget {
public:
    explicit NodeEditor(Studio& s) : m_s(s) { manualLayout().childTransform(m_zoom, m_pan); }
    bool isFocusable() const override { return true; }

    void addView(Node* n) {
        NodeView& v = add<NodeView>(m_s, n);
        v.visible(false); // updateVisibility() shows it if on screen
        v.setFrame(nodeRect(*n));
        n->view = &v;
    }
    void removeView(Node* n) {
        detach(n->view);
        n->view = nullptr;
    }
    void place(Node* n) { n->view->setFrame(nodeRect(*n)); }

    // ---- view: content point `pan` at the top-left, `zoom` window px per content px ----
    SkPoint pan() const { return m_pan; }
    float zoom() const { return m_zoom; }
    void setView(SkPoint pan, float zoom) {
        zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
        if (pan == m_pan && zoom == m_zoom) return;
        m_pan = pan;
        m_zoom = zoom;
        childTransform(m_zoom, m_pan); // repaints; no layout
        updateVisibility();
        if (onViewChanged) onViewChanged();
    }
    void setPan(SkPoint p) { setView(p, m_zoom); }
    void zoomAt(SkPoint w, float factor) { // keeps the content point under window point `w` in place
        const SkPoint c = toContent(w);
        const float z = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
        setView(c - SkVector{(w.x() - m_bounds.left()) / z, (w.y() - m_bounds.top()) / z}, z);
    }
    void zoomBy(float factor) { zoomAt({m_bounds.centerX(), m_bounds.centerY()}, factor); }
    void frameRect(SkRect r, float maxZoom) {
        if (r.isEmpty() || m_bounds.isEmpty()) return;
        r.outset(40, 40);
        const float z = std::clamp(std::min(m_bounds.width() / r.width(), m_bounds.height() / r.height()), kMinZoom, maxZoom);
        setView({r.centerX() - m_bounds.width() / 2 / z, r.centerY() - m_bounds.height() / 2 / z}, z);
    }
    void frame(const Node* n) { frameRect(nodeRect(*n), std::max(m_zoom, 1.0f)); }
    void frameSelection() {
        SkRect r = SkRect::MakeEmpty();
        for (Node* n : m_s.doc.selection()) r.join(nodeRect(*n));
        frameRect(r, std::max(m_zoom, 1.0f));
    }
    void frameAll() { frameRect(contentBounds(), kMaxZoom); }
    SkPoint viewCenter() const { return toContent({m_bounds.centerX(), m_bounds.centerY()}); }
    std::optional<SkPoint> mouseContent() const {
        if (!isHovered()) return std::nullopt;
        return toContent(m_mouse);
    }
    std::function<void()> onViewChanged;

    // Zoomed out past kLodZoom, draw boxes instead of node widgets (unless turned off).
    bool lod() const { return m_lodEnabled && m_zoom < kLodZoom; }
    bool lodEnabled() const { return m_lodEnabled; }
    void setLodEnabled(bool on) {
        if (on == m_lodEnabled) return;
        m_lodEnabled = on;
        updateVisibility();
        markPaintDirty();
        if (onViewChanged) onViewChanged();
    }
    void updateVisibility() {
        const SkRect vp = viewport().makeOutset(32, 32);
        for (auto& n : m_s.doc.nodes) {
            NodeView* v = n->view;
            const bool on = !lod() && SkRect::Intersects(vp, nodeRect(*n));
            if (on == v->isVisible()) continue;
            v->visible(on);
            if (on && v->stale) v->refresh(true);
        }
    }
    void refreshValues(bool all) {
        for (auto& n : m_s.doc.nodes) {
            if (n->view->isVisible()) n->view->refresh(all);
            else n->view->stale = true;
        }
    }

    void onLayout() override { updateVisibility(); }
    bool onKey(const KeyEvent& e) override {
        if (e.action != KeyAction::Release && e.key == Key::Home) { // view all (Home elsewhere: time 0)
            frameAll();
            return true;
        }
        return m_s.handleKey(e, *this);
    }
    // Scroll pans; with Ctrl or Cmd held it zooms around the pointer.
    bool onScroll(float dx, float dy) override {
        if (Application::get().modifiers() & (ModCtrl | ModSuper)) zoomAt(m_mouse, std::pow(1.0025f, dy));
        else setPan(m_pan - SkVector{dx / m_zoom, dy / m_zoom});
        return true;
    }
    // The minimap is on top of the nodes: it takes the pointer before them.
    Widget* hitTest(float x, float y) override {
        if (isVisible() && minimapRect().contains(x, y)) return this;
        return Widget::hitTest(x, y);
    }

    bool onMouseDown(const MouseEvent& e) override {
        const SkPoint w{e.x, e.y}, c = toContent(w);
        m_press = m_last = m_mouse = w;
        m_moved = false;
        m_shift = e.mods & ModShift;
        if (e.button == MouseButton::Left && minimapRect().contains(e.x, e.y)) {
            minimapJump(w);
            return begin(Drag::Minimap);
        }
        if (e.button == MouseButton::Middle || (e.button == MouseButton::Left && (e.mods & ModAlt))) return begin(Drag::Pan);
        if (e.button == MouseButton::Right) {
            contextMenu(w);
            return begin(Drag::None);
        }
        if (e.button != MouseButton::Left) return false;
        if (auto [n, input] = portAt(c); n) {
            if (input < 0) m_linkFrom = n;
            else if ((m_linkFrom = n->inputs[input])) { // pick the wire up from its input
                n->inputs[input] = nullptr;
                m_s.graphChanged();
            }
            if (m_linkFrom) return begin(Drag::Link);
        }
        if (Node* n = nodeAt(c)) {
            raise(n->view);
            m_pressNode = n;
            if (m_shift) n->selected = !n->selected;
            else if (!n->selected) m_s.doc.selectOnly(n);
            m_s.selectionChanged();
            return begin(n->selected ? Drag::Move : Drag::None);
        }
        if (!m_shift) {
            m_s.doc.selectOnly(nullptr);
            m_s.selectionChanged();
        }
        return begin(Drag::Box);
    }
    bool onMouseMove(const MouseEvent& e) override {
        const SkPoint w{e.x, e.y};
        m_mouse = w;
        if (m_drag == Drag::None) return false;
        if (!m_moved && SkPoint::Distance(w, m_press) < 3) return true;
        m_moved = true;
        const SkVector d = (w - m_last) * (1 / m_zoom); // content units
        m_last = w;
        switch (m_drag) {
        case Drag::Pan: setPan(m_pan - d); break;
        case Drag::Minimap: minimapJump(w); break;
        case Drag::Move:
            for (auto& n : m_s.doc.nodes)
                if (n->selected) n->pos += d, place(n.get());
            updateVisibility();
            markPaintDirty(); // wires, minimap
            break;
        default: markPaintDirty(); break; // box / wire preview
        }
        return true;
    }
    bool onMouseUp(const MouseEvent& e) override {
        const Drag d = m_drag;
        m_drag = Drag::None;
        const SkPoint c = toContent({e.x, e.y});
        if (d == Drag::Move && !m_moved && !m_shift && m_pressNode) {
            m_s.doc.selectOnly(m_pressNode);
            m_s.selectionChanged();
        } else if (d == Drag::Box) {
            SkRect r;
            r.set(toContent(m_press), c);
            for (auto& n : m_s.doc.nodes)
                if (SkRect::Intersects(r, nodeRect(*n))) n->selected = true;
            m_s.selectionChanged();
        } else if (d == Drag::Link) {
            if (auto [n, input] = portAt(c); n && input >= 0) m_s.doc.connect(m_linkFrom, n, input);
            m_s.graphChanged();
        }
        m_pressNode = m_linkFrom = nullptr;
        return true;
    }

    // Background, grid, wires and (zoomed out) node boxes; NodeViews paint on top. Clipped: nothing
    // drawn here may leak onto neighbouring widgets.
    void onDraw(SkCanvas* c) override {
        c->save();
        c->clipRect(m_bounds);
        SkPaint p;
        p.setColor(kCanvas);
        c->drawRect(m_bounds, p);
        const SkRect clip = c->getLocalClipBounds();
        // dot grid, every 24 content px (coarser when zoomed out so dots stay >= 12 px apart)
        float g = 24 * m_zoom;
        while (g < 12) g *= 4;
        float ox = -std::fmod(m_pan.x() * m_zoom, g), oy = -std::fmod(m_pan.y() * m_zoom, g);
        if (ox > 0) ox -= g;
        if (oy > 0) oy -= g;
        std::vector<SkPoint> dots;
        for (float y = m_bounds.top() + oy; y < m_bounds.bottom(); y += g)
            for (float x = m_bounds.left() + ox; x < m_bounds.right(); x += g)
                if (clip.contains(x, y)) dots.push_back({x, y});
        p.setColor(SkColorSetRGB(46, 46, 50));
        p.setStrokeWidth(2);
        c->drawPoints(SkCanvas::kPoints_PointMode, dots, p);

        p.setAntiAlias(true);
        p.setStyle(SkPaint::kStroke_Style);
        const float wireW = std::clamp(2.0f * m_zoom, 1.0f, 3.0f);
        for (auto& n : m_s.doc.nodes) {
            const TypeDef& d = typeDef(n->type);
            for (int i = 0; i < int(d.inputs.size()); ++i) {
                const Node* src = n->inputs[i];
                if (!src) continue;
                const bool hot = src->selected || n->selected;
                p.setStrokeWidth(hot ? wireW * 1.5f : wireW);
                p.setColor(SkColorSetA(portColor(d.inputs[i]), hot ? 255 : 170));
                drawWire(c, p, contentToWindow(src->pos + outputPort()), contentToWindow(n->pos + inputPort(*n, i)), clip, m_zoom);
            }
        }
        if (lod()) drawNodeBoxes(c, clip);
        c->restore();
    }
    void onDrawOver(SkCanvas* c) override {
        c->save();
        c->clipRect(m_bounds);
        SkPaint p;
        p.setAntiAlias(true);
        if (m_drag == Drag::Link) {
            p.setStyle(SkPaint::kStroke_Style);
            p.setStrokeWidth(2.5f);
            p.setColor(portColor(Port(int(m_linkFrom->type))));
            drawWire(c, p, contentToWindow(m_linkFrom->pos + outputPort()), m_mouse, c->getLocalClipBounds(), m_zoom);
        } else if (m_drag == Drag::Box && m_moved) {
            SkRect r;
            r.set(m_press, m_mouse);
            p.setColor(SkColorSetARGB(40, 255, 255, 255));
            c->drawRect(r, p);
            p.setStyle(SkPaint::kStroke_Style);
            p.setColor(SkColorSetARGB(160, 255, 255, 255));
            c->drawRect(r, p);
        }
        drawMinimap(c);
        c->restore();
    }

private:
    enum class Drag { None, Pan, Move, Box, Link, Minimap };
    struct PortHit {
        Node* node = nullptr;
        int input = -1; // -1 = the output port
    };

    bool begin(Drag d) {
        m_drag = d;
        return true;
    }
    SkPoint toContent(SkPoint w) const {
        return {m_pan.x() + (w.x() - m_bounds.left()) / m_zoom, m_pan.y() + (w.y() - m_bounds.top()) / m_zoom};
    }
    SkPoint contentToWindow(SkPoint c) const {
        return {m_bounds.left() + (c.x() - m_pan.x()) * m_zoom, m_bounds.top() + (c.y() - m_pan.y()) * m_zoom};
    }
    SkRect viewport() const { return SkRect::MakeXYWH(m_pan.x(), m_pan.y(), m_bounds.width() / m_zoom, m_bounds.height() / m_zoom); }
    SkRect contentBounds() const {
        SkRect r = SkRect::MakeEmpty();
        for (auto& n : m_s.doc.nodes) r.join(nodeRect(*n));
        return r;
    }

    Node* nodeAt(SkPoint c) const { // topmost = last child
        for (auto it = children().rbegin(); it != children().rend(); ++it) {
            auto* v = static_cast<NodeView*>(it->get());
            if (nodeRect(*v->node()).contains(c.x(), c.y())) return v->node();
        }
        return nullptr;
    }
    PortHit portAt(SkPoint c) const {
        const float r = std::max(kPortR + 4, 6 / m_zoom); // at least 6 window px
        for (auto& n : m_s.doc.nodes) {
            auto near = [&](SkPoint p) { return SkPoint::Distance(c, n->pos + p) <= r; };
            const TypeDef& d = typeDef(n->type);
            if (d.hasOutput && near(outputPort())) return {n.get(), -1};
            for (int i = 0; i < int(d.inputs.size()); ++i)
                if (near(inputPort(*n, i))) return {n.get(), i};
        }
        return {};
    }
    void raise(NodeView* v) { // paint (and hit) on top
        if (children().back().get() == v) return;
        adopt(detach(v));
        markPaintDirty();
    }
    void contextMenu(SkPoint w) {
        const SkPoint c = toContent(w);
        Node* hit = nodeAt(c);
        if (hit && !hit->selected) {
            m_s.doc.selectOnly(hit);
            m_s.selectionChanged();
        }
        std::vector<std::string> items;
        for (int t = 0; t < kNodeTypes; ++t) items.push_back(std::string("Add ") + typeDef(NodeType(t)).name);
        if (hit)
            for (const char* s : {"Insert Keyframes", "Duplicate", "Delete"}) items.push_back(s);
        openMenu(*this, w, items, [this, c](int i) {
            if (i < kNodeTypes) m_s.addNode(NodeType(i), c);
            else if (i == kNodeTypes) m_s.insertKeys();
            else if (i == kNodeTypes + 1) m_s.paste(m_s.doc.copySelected(), {30, 30});
            else m_s.deleteSelected();
        });
    }

    // Zoomed out: a box per node with its header colour, no widgets.
    void drawNodeBoxes(SkCanvas* c, const SkRect& clip) {
        SkPaint p;
        p.setAntiAlias(m_zoom > 0.1f);
        const float radius = 6 * m_zoom;
        for (auto& n : m_s.doc.nodes) {
            const SkRect nr = nodeRect(*n);
            const SkPoint a = contentToWindow({nr.left(), nr.top()}), b = contentToWindow({nr.right(), nr.bottom()});
            const SkRect r = SkRect::MakeLTRB(a.x(), a.y(), b.x(), b.y());
            if (!SkRect::Intersects(r, clip)) continue;
            p.setStyle(SkPaint::kFill_Style);
            p.setColor(SkColorSetRGB(46, 46, 50));
            c->drawRoundRect(r, radius, radius, p);
            p.setColor(typeDef(n->type).color);
            c->drawRect(SkRect::MakeLTRB(r.left(), r.top(), r.right(), r.top() + kTitleH * m_zoom), p);
            if (n->selected) {
                p.setStyle(SkPaint::kStroke_Style);
                p.setStrokeWidth(1.5f);
                p.setColor(kSelect);
                c->drawRoundRect(r, radius, radius, p);
            }
        }
    }

    // ---- minimap ----
    // Content bounds (all nodes) fitted into a corner box; the view is the white frame.
    SkRect minimapRect() const {
        const SkRect cb = contentBounds();
        if (cb.isEmpty() || m_bounds.width() < 3 * kMiniW || m_bounds.height() < 2 * kMiniMaxH) return SkRect::MakeEmpty();
        const float h = std::min(kMiniMaxH, kMiniW * cb.height() / cb.width()), w = h * cb.width() / cb.height();
        return SkRect::MakeXYWH(m_bounds.right() - kMiniMargin - w, m_bounds.bottom() - kMiniMargin - h, w, h);
    }
    void minimapJump(SkPoint w) { // centre the view on the content point under w
        const SkRect mr = minimapRect(), cb = contentBounds();
        if (mr.isEmpty()) return;
        const SkPoint c{cb.left() + (w.x() - mr.left()) / mr.width() * cb.width(), cb.top() + (w.y() - mr.top()) / mr.height() * cb.height()};
        setPan(c - SkVector{m_bounds.width() / 2 / m_zoom, m_bounds.height() / 2 / m_zoom});
    }
    void drawMinimap(SkCanvas* c) {
        const SkRect mr = minimapRect();
        if (mr.isEmpty() || !SkRect::Intersects(mr.makeOutset(2, 2), c->getLocalClipBounds())) return;
        const SkRect cb = contentBounds();
        const float k = mr.width() / cb.width();
        auto map = [&](const SkRect& r) {
            return SkRect::MakeXYWH(mr.left() + (r.left() - cb.left()) * k, mr.top() + (r.top() - cb.top()) * k, r.width() * k, r.height() * k);
        };
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(SkColorSetARGB(220, 18, 18, 20));
        c->drawRoundRect(mr.makeOutset(4, 4), 5, 5, p);
        p.setAntiAlias(false);
        for (auto& n : m_s.doc.nodes) {
            p.setColor(n->selected ? kSelect : SkColorSetA(typeDef(n->type).color, 200));
            c->drawRect(map(nodeRect(*n)), p);
        }
        SkRect view = map(viewport());
        view.intersect(mr.makeOutset(4, 4));
        p.setAntiAlias(true);
        p.setStyle(SkPaint::kStroke_Style);
        p.setStrokeWidth(1.5f);
        p.setColor(SK_ColorWHITE);
        c->drawRect(view, p);
    }

    Studio& m_s;
    SkPoint m_pan{0, 0};
    float m_zoom = 1.0f;
    bool m_lodEnabled = true;
    Drag m_drag = Drag::None;
    SkPoint m_press{}, m_last{}, m_mouse{}; // window coords
    bool m_moved = false, m_shift = false;
    Node* m_pressNode = nullptr;
    Node* m_linkFrom = nullptr;
};

class EditorPanel : public VBox {
public:
    explicit EditorPanel(Studio& s) {
        add<Panel>(kPanel).direction(Direction::Row).height(34).padding(Edge::Horizontal, 8).gap(6).alignItems(Align::Center)
            .build([&](Panel& bar) {
                bar.add<Button>("Add Node").onClick([this, &s] {
                    std::vector<std::string> names;
                    for (int t = 0; t < kNodeTypes; ++t) names.push_back(typeDef(NodeType(t)).name);
                    const SkRect b = m_add->mapToWindow(m_add->bounds());
                    openMenu(*m_add, b.makeOutset(0, 2), names, [&s](int i) { s.addNode(NodeType(i), s.editor->viewCenter()); });
                }).ref(m_add).alignSelf(Align::Center); // Button aligns itself to the top by default
                bar.add<Button>("Frame Selected").onClick([&s] { s.editor->frameSelection(); }).alignSelf(Align::Center);
                bar.add<Button>("Frame All").onClick([&s] { s.editor->frameAll(); }).alignSelf(Align::Center);
                bar.add<Button>("-").onClick([&s] { s.editor->zoomBy(1 / 1.25f); }).alignSelf(Align::Center);
                bar.add<Button>("+").onClick([&s] { s.editor->zoomBy(1.25f); }).alignSelf(Align::Center);
                bar.add<ToggleButton>("LOD", true, [&s](bool on) { s.editor->setLodEnabled(on); });
                bar.add<Label>("Right-click: menu   Drag ports: wire   Scroll: pan   Ctrl/Cmd+scroll: zoom   Home: view all   "
                               "Ctrl/Cmd+C V D A, Del, K, F, Space")
                    .fontSize(11).color(kMuted).shrink(1).grow(1).basis(0).margin(Edge::Left, 6);
            });
        s.editor = &add<NodeEditor>(s).grow(1).basis(0);
    }

private:
    Button* m_add = nullptr;
};

// ---------------------------------------------------------------- node list (left)

class ListRow : public HBox {
public:
    explicit ListRow(Studio& s) : m_s(s) {
        padding(Edge::Horizontal, 10).gap(8).alignItems(Align::Center);
        m_chip = &add<Panel>().size(8, 8);
        m_name = &add<Label>("").fontSize(12).grow(1).shrink(1).basis(0);
        m_type = &add<Label>("").fontSize(11).color(kMuted);
    }
    void bind(Node* n, int index) {
        if (index != m_index) m_index = index, markPaintDirty();
        if (n == m_node && n->name == m_name->text()) return; // the name check catches a reused address
        m_node = n;
        m_name->text(n->name);
        m_type->text(typeDef(n->type).name);
        m_chip->background(typeDef(n->type).color);
    }
    bool onMouseDown(const MouseEvent& e) override { return e.button == MouseButton::Left; }
    bool onMouseClick(const MouseEvent& e) override {
        m_s.select(m_node, e.mods, true);
        return true;
    }
    void onMouseEnter() override { markPaintDirty(); }
    void onMouseLeave() override { markPaintDirty(); }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setColor(m_node && m_node->selected ? kRowSel : isHovered() ? SkColorSetRGB(46, 46, 50) : m_index % 2 ? kRowB : kRowA);
        c->drawRect(m_bounds, p);
    }

private:
    Studio& m_s;
    Node* m_node = nullptr;
    int m_index = -1;
    Panel* m_chip;
    Label *m_name, *m_type;
};

class NodeList : public VirtualList {
public:
    explicit NodeList(Studio& s) : VirtualList(kListRowH), m_s(s) {
        rows([this](Widget& parent) -> Widget& { return parent.add<ListRow>(m_s); },
             [this](Widget& row, int i) { static_cast<ListRow&>(row).bind(m_rows[i], i); });
    }
    Label* countLabel = nullptr;

    void rebuild() {
        m_rows.clear();
        for (auto& n : m_s.doc.nodes)
            if (m_filter.empty() || lower(n->name).find(m_filter) != std::string::npos) m_rows.push_back(n.get());
        count(int(m_rows.size()));
        refresh();
        const std::string total = std::to_string(m_s.doc.nodes.size());
        if (countLabel) countLabel->text(m_filter.empty() ? total : std::to_string(m_rows.size()) + " / " + total);
    }
    void setFilter(const std::string& f) {
        m_filter = lower(f);
        setScrollY(0);
        rebuild();
    }
    bool isFocusable() const override { return true; }
    bool onKey(const KeyEvent& e) override { return m_s.handleKey(e, *this); }

private:
    static std::string lower(std::string s) {
        for (char& ch : s) ch = char(std::tolower((unsigned char)ch));
        return s;
    }
    Studio& m_s;
    std::string m_filter;
    std::vector<Node*> m_rows;
};

class NodeListPanel : public Panel {
public:
    explicit NodeListPanel(Studio& s) : Panel(kPanel) {
        Label* count = nullptr;
        add<VBox>().padding(8).gap(6).build([&](VBox& top) {
            top.add<HBox>().alignItems(Align::Center).build([&](HBox& h) {
                h.add<Label>("Nodes").fontSize(13).weight(600).grow(1);
                count = &h.add<Label>("").fontSize(11).color(kMuted);
            });
            top.add<InputText>("Filter nodes...").onChange([&s](const std::string& f) { s.list->setFilter(f); });
        });
        s.list = &add<NodeList>(s);
        s.list->countLabel = count;
    }
};

// ---------------------------------------------------------------- timeline (bottom)

class Ruler : public Widget {
public:
    explicit Ruler(Studio& s) : m_s(s) {
        manualLayout();
        for (int sec = 0; sec <= int(kDuration); ++sec) m_labels.push_back(&add<Label>(std::to_string(sec)).fontSize(10).color(kMuted));
    }
    void onLayout() override {
        const float px = (m_bounds.width() - 2 * kLanePad) / kDuration; // per second
        const int every = px >= 24 ? 1 : px >= 12 ? 2 : 5;
        for (int sec = 0; sec < int(m_labels.size()); ++sec) {
            const bool on = sec % every == 0;
            if (m_labels[sec]->isVisible() != on) m_labels[sec]->visible(on);
            const float x = std::min(kLanePad + sec * px + 3, m_bounds.width() - 16);
            if (on) m_labels[sec]->setFrame(SkRect::MakeXYWH(x, 1, 24, 14));
        }
    }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setColor(SkColorSetRGB(30, 30, 32));
        c->drawRect(m_bounds, p);
        p.setColor(SkColorSetRGB(80, 80, 86));
        const float px = (m_bounds.width() - 2 * kLanePad) / kDuration;
        const float step = px * 0.25f >= 5 ? 0.25f : px >= 5 ? 1.0f : 5.0f;
        for (float t = 0; t <= kDuration + 1e-3f; t += step) {
            const bool major = std::abs(t - std::round(t)) < 1e-3f;
            const float x = laneX(m_bounds, t);
            c->drawRect(SkRect::MakeXYWH(x, m_bounds.bottom() - (major ? 9 : 4), 1, major ? 9 : 4), p);
        }
        // playhead handle
        const float x = laneX(m_bounds, m_s.time);
        p.setAntiAlias(true);
        p.setColor(kPlayhead);
        c->drawRoundRect(SkRect::MakeXYWH(x - 5, m_bounds.top() + 2, 10, m_bounds.height() - 4), 3, 3, p);
    }
    bool onMouseDown(const MouseEvent& e) override { // scrub
        if (e.button != MouseButton::Left) return false;
        m_down = true;
        m_s.setTime(snapFrame(laneT(m_bounds, e.x)));
        return true;
    }
    bool onMouseMove(const MouseEvent& e) override {
        if (!m_down) return false; // hover
        m_s.setTime(snapFrame(laneT(m_bounds, e.x)));
        return true;
    }
    bool onMouseUp(const MouseEvent&) override {
        m_down = false;
        return true;
    }
    Cursor cursor() const override { return Cursor::HResize; }

private:
    Studio& m_s;
    std::vector<Label*> m_labels;
    bool m_down = false;
};

// One node's keys over time. Click a diamond to select (Shift adds), drag to move the selected keys;
// click empty space to select the node and scrub.
class Lane : public Widget {
public:
    explicit Lane(Studio& s) : m_s(s) { grow(1).basis(0); }
    void bind(Node* n, int index) {
        if (n != m_node || index != m_index) m_node = n, m_index = index, markPaintDirty();
    }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setColor(m_node && m_node->selected ? kRowSel : m_index % 2 ? kRowB : kRowA);
        c->drawRect(m_bounds, p);
        for (int sec = 0; sec <= int(kDuration); ++sec) {
            p.setColor(sec % 5 ? SkColorSetRGB(38, 38, 42) : SkColorSetRGB(48, 48, 54));
            c->drawRect(SkRect::MakeXYWH(laneX(m_bounds, float(sec)), m_bounds.top(), 1, m_bounds.height()), p);
        }
        if (!m_node) return;
        const std::vector<float> ts = m_node->keyTimes();
        if (ts.size() > 1) { // hold bar between the first and last key
            p.setColor(SkColorSetARGB(70, 230, 200, 70));
            c->drawRect(SkRect::MakeLTRB(laneX(m_bounds, ts.front()), m_bounds.centerY() - 1.5f, laneX(m_bounds, ts.back()),
                                         m_bounds.centerY() + 1.5f), p);
        }
        for (float t : ts) {
            const bool sel = m_s.keySelected(m_node, t);
            drawDiamond(c, {laneX(m_bounds, t), m_bounds.centerY()}, 5.5f, sel ? kSelect : kKey, true);
            drawDiamond(c, {laneX(m_bounds, t), m_bounds.centerY()}, 5.5f, SkColorSetRGB(20, 20, 22), false);
        }
    }
    bool onMouseDown(const MouseEvent& e) override {
        if (e.button != MouseButton::Left || !m_node) return false;
        float hit = -1;
        for (float t : m_node->keyTimes())
            if (std::abs(laneX(m_bounds, t) - e.x) <= 6) hit = t;
        if (hit < 0) {
            m_s.selectedKeys.clear();
            m_s.select(m_node, e.mods, false);
            m_s.setTime(snapFrame(laneT(m_bounds, e.x)));
            m_mode = Mode::Scrub;
            return true;
        }
        auto& sel = m_s.selectedKeys;
        const bool was = m_s.keySelected(m_node, hit);
        if (e.mods & ModShift) {
            if (was) std::erase_if(sel, [&](const KeyRef& k) { return k.first == m_node && std::abs(k.second - hit) < 0.5f / kFps; });
            else sel.push_back({m_node, hit});
        } else if (!was) {
            sel = {{m_node, hit}};
        }
        m_s.keysChanged({m_node}); // repaints the key selection
        // Drags restart from these originals each move, so crossing other keys never destroys them.
        m_origSel = sel;
        m_orig.clear();
        for (auto& [n, t] : sel)
            if (std::none_of(m_orig.begin(), m_orig.end(), [n = n](auto& o) { return o.first == n; })) m_orig.push_back({n, n->params});
        m_pressX = e.x;
        m_mode = Mode::Move;
        return true;
    }
    bool onMouseMove(const MouseEvent& e) override {
        if (m_mode == Mode::Scrub) m_s.setTime(snapFrame(laneT(m_bounds, e.x)));
        if (m_mode != Mode::Move || m_origSel.empty()) return m_mode != Mode::None;
        const float pxPerSec = (m_bounds.width() - 2 * kLanePad) / kDuration;
        float lo = kDuration, hi = 0;
        for (auto& [n, t] : m_origSel) lo = std::min(lo, t), hi = std::max(hi, t);
        const float dt = std::clamp(std::round((e.x - m_pressX) / pxPerSec * kFps) / kFps, -lo, kDuration - hi);
        for (auto& [n, params] : m_orig) n->params = params;
        m_s.selectedKeys = moveKeys(m_origSel, dt);
        std::vector<Node*> touched;
        for (auto& [n, params] : m_orig) touched.push_back(n);
        m_s.keysChanged(touched);
        return true;
    }
    bool onMouseUp(const MouseEvent&) override {
        m_mode = Mode::None;
        return true;
    }

private:
    enum class Mode { None, Scrub, Move };
    Studio& m_s;
    Node* m_node = nullptr;
    int m_index = 0;
    Mode m_mode = Mode::None;
    float m_pressX = 0;
    std::vector<KeyRef> m_origSel;
    std::vector<std::pair<Node*, std::vector<Param>>> m_orig;
};

class TrackHeader : public HBox {
public:
    explicit TrackHeader(Studio& s) : m_s(s) {
        width(kHeaderW).padding(Edge::Horizontal, 8).gap(6).alignItems(Align::Center);
        m_chip = &add<Panel>().size(8, 8);
        m_name = &add<Label>("").fontSize(11).grow(1).shrink(1).basis(0);
    }
    void bind(Node* n, int index) {
        if (index != m_index) m_index = index, markPaintDirty();
        if (n == m_node && n->name == m_name->text()) return;
        m_node = n;
        m_name->text(n->name);
        m_chip->background(typeDef(n->type).color);
    }
    bool onMouseDown(const MouseEvent& e) override { return e.button == MouseButton::Left; }
    bool onMouseClick(const MouseEvent& e) override {
        m_s.select(m_node, e.mods, true);
        return true;
    }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setColor(m_node && m_node->selected ? kRowSel : m_index % 2 ? kRowB : kRowA);
        c->drawRect(m_bounds, p);
        p.setColor(SkColorSetRGB(18, 18, 20));
        c->drawRect(SkRect::MakeXYWH(m_bounds.right() - 1, m_bounds.top(), 1, m_bounds.height()), p);
    }

private:
    Studio& m_s;
    Node* m_node = nullptr;
    int m_index = -1;
    Panel* m_chip;
    Label* m_name;
};

class TrackRow : public HBox {
public:
    explicit TrackRow(Studio& s) {
        m_header = &add<TrackHeader>(s);
        m_lane = &add<Lane>(s);
    }
    void bind(Node* n, int i) {
        m_header->bind(n, i);
        m_lane->bind(n, i);
    }

private:
    TrackHeader* m_header;
    Lane* m_lane;
};

class TrackView : public VirtualList {
public:
    explicit TrackView(Studio& s) : VirtualList(kTrackH), m_s(s) {
        rows([this](Widget& parent) -> Widget& { return parent.add<TrackRow>(m_s); },
             [this](Widget& row, int i) { static_cast<TrackRow&>(row).bind(m_s.doc.nodes[i].get(), i); });
    }
    void rebuild() {
        count(int(m_s.doc.nodes.size()));
        refresh();
    }
    SkRect laneRect() const { return SkRect::MakeLTRB(m_bounds.left() + kHeaderW, m_bounds.top(), m_bounds.right(), m_bounds.bottom()); }
    // Repaint just the strips under the old and new playhead.
    void playheadMoved(float old) {
        for (float t : {old, m_s.time}) {
            const float x = laneX(laneRect(), t);
            invalidateRect(SkRect::MakeLTRB(x - 2, m_bounds.top(), x + 2, m_bounds.bottom()));
        }
    }
    void onDrawOver(SkCanvas* c) override {
        VirtualList::onDrawOver(c); // scrollbar
        SkPaint p;
        p.setColor(kPlayhead);
        const float x = laneX(laneRect(), m_s.time);
        c->drawRect(SkRect::MakeLTRB(x - 1, m_bounds.top(), x + 1, m_bounds.bottom()), p);
    }

private:
    Studio& m_s;
};

class TimelinePanel : public Panel {
public:
    explicit TimelinePanel(Studio& s) : Panel(kPanel), m_s(s) {
        add<HBox>().height(34).padding(Edge::Horizontal, 8).gap(6).alignItems(Align::Center).build([&](HBox& bar) {
            bar.add<ToolButton>(ToolButton::Start, [&s] { s.setTime(0); });
            m_play = &bar.add<ToolButton>(ToolButton::Play, [&s] { s.togglePlay(); });
            bar.add<ToolButton>(ToolButton::End, [&s] { s.setTime(kDuration); });
            m_time = &bar.add<Label>("").fontSize(12).width(200).margin(Edge::Left, 6);
            bar.add<Widget>().grow(1);
            bar.add<Button>("Insert Keys (K)").onClick([&s] { s.insertKeys(); }).alignSelf(Align::Center);
            bar.add<Button>("Delete Keys").onClick([&s] { s.deleteSelectedKeys(); }).alignSelf(Align::Center);
        });
        add<HBox>().height(24).build([&](HBox& row) {
            row.add<HBox>().width(kHeaderW).alignItems(Align::Center).padding(Edge::Horizontal, 8).build([&](HBox& h) {
                m_count = &h.add<Label>("").fontSize(11).color(kMuted);
            });
            m_ruler = &row.add<Ruler>(s).grow(1).basis(0);
        });
        m_tracks = &add<TrackView>(s);
        updateTime();
    }
    TrackView& tracks() { return *m_tracks; }
    bool isFocusable() const override { return true; }
    bool onKey(const KeyEvent& e) override {
        if (e.action != KeyAction::Release && (e.key == Key::Delete || e.key == Key::Backspace) && !m_s.selectedKeys.empty()) {
            m_s.deleteSelectedKeys();
            return true;
        }
        return m_s.handleKey(e, *this);
    }
    void timeMoved(float old) {
        m_ruler->markPaintDirty();
        m_tracks->playheadMoved(old);
        updateTime();
    }
    void rebuild() {
        m_tracks->rebuild();
        m_count->text(std::to_string(m_s.doc.nodes.size()) + " tracks");
    }
    void repaint() { m_tracks->refresh(); }
    // Playback is a Widget animation: one tick per frame while playing, looping at the end.
    void play() {
        const float start = m_s.time >= kDuration ? 0 : m_s.time, dur = kDuration - start;
        animate(dur, [this, start, dur](float u) { m_s.setTime(start + u * dur, true); }, [this] { play(); }, Ease::Linear);
        m_play->setIcon(ToolButton::Pause);
    }
    void stop() {
        m_anim.stop();
        m_play->setIcon(ToolButton::Play);
    }

private:
    void updateTime() {
        char b[64];
        std::snprintf(b, sizeof b, "%05.2f s    frame %d / %d", m_s.time, int(std::round(m_s.time * kFps)), int(kDuration * kFps));
        m_time->text(b);
    }
    Studio& m_s;
    ToolButton* m_play;
    Label *m_time, *m_count;
    Ruler* m_ruler;
    TrackView* m_tracks;
};

// ---------------------------------------------------------------- preview (right)

SkPath shapePath(int kind, float w, float h) { // half extents, centred on 0
    SkPathBuilder b;
    auto polygon = [&](int n, float inner) { // inner < 1: a star with n points
        const int pts = inner < 1 ? n * 2 : n;
        for (int i = 0; i < pts; ++i) {
            const float a = -std::numbers::pi_v<float> / 2 + i * 2 * std::numbers::pi_v<float> / pts;
            const float r = inner < 1 && i % 2 ? inner : 1;
            const SkPoint pt{std::cos(a) * w * r, std::sin(a) * h * r};
            i ? b.lineTo(pt) : b.moveTo(pt);
        }
        b.close();
    };
    switch (kind) {
    case 0: b.addOval(SkRect::MakeLTRB(-w, -h, w, h)); break;
    case 1: b.addRect(SkRect::MakeLTRB(-w, -h, w, h)); break;
    case 2: polygon(3, 1); break;
    case 3: polygon(5, 0.45f); break;
    default: polygon(6, 1); break;
    }
    return b.detach();
}

class Preview : public Widget {
public:
    explicit Preview(Studio& s) : m_s(s) {
        padding(10).alignItems(Align::Start);
        m_info = &add<Label>("").fontSize(11).color(kMuted);
    }
    bool isFocusable() const override { return true; }
    bool onKey(const KeyEvent& e) override { return m_s.handleKey(e, *this); }
    void updateInfo() {
        char b[64];
        std::snprintf(b, sizeof b, "%d x %d   %.2f s", int(kSceneW), int(kSceneH), m_s.time);
        m_info->text(b);
    }
    // The 16:9 stage, centred below the info line.
    SkRect stage() const {
        const SkRect r = SkRect::MakeLTRB(m_bounds.left() + 10, m_bounds.top() + 30, m_bounds.right() - 10, m_bounds.bottom() - 10);
        const float k = std::max(0.0f, std::min(r.width() / kSceneW, r.height() / kSceneH));
        return SkRect::MakeXYWH(r.centerX() - kSceneW * k / 2, r.centerY() - kSceneH * k / 2, kSceneW * k, kSceneH * k);
    }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setColor(SkColorSetRGB(24, 24, 26));
        c->drawRect(m_bounds, p);
        const SkRect st = stage();
        const float k = st.width() / kSceneW;
        p.setColor(SkColorSetRGB(10, 10, 14));
        c->drawRect(st, p);
        c->save();
        c->clipRect(st);
        for (auto& n : m_s.doc.nodes)
            if (n->type == NodeType::Output) drawShape(c, evaluate(*n, m_s.time), st, k, n->selected);
        c->restore();
    }
    bool onMouseDown(const MouseEvent& e) override { // pick the topmost shape (by its unrotated box)
        if (e.button != MouseButton::Left) return false;
        const SkRect st = stage();
        const float k = st.width() / kSceneW;
        Node* hit = nullptr;
        for (auto& n : m_s.doc.nodes) {
            if (n->type != NodeType::Output) continue;
            const Drawn d = evaluate(*n, m_s.time);
            if (SkRect::MakeXYWH(st.left() + (d.x - d.w / 2) * k, st.top() + (d.y - d.h / 2) * k, d.w * k, d.h * k).contains(e.x, e.y))
                hit = n.get();
        }
        if (hit) m_s.select(hit, e.mods, true);
        else if (!(e.mods & ModShift)) m_s.doc.selectOnly(nullptr), m_s.selectionChanged();
        return true;
    }

private:
    static void drawShape(SkCanvas* c, const Drawn& d, const SkRect& st, float k, bool selected) {
        static constexpr SkBlendMode kModes[] = {SkBlendMode::kSrcOver, SkBlendMode::kPlus, SkBlendMode::kMultiply, SkBlendMode::kScreen};
        float hsv[3] = {std::fmod(std::fmod(d.hue, 360.0f) + 360.0f, 360.0f), std::clamp(d.sat, 0.0f, 1.0f), std::clamp(d.val, 0.0f, 1.0f)};
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(SkHSVToColor(uint8_t(std::clamp(d.alpha * d.opacity, 0.0f, 1.0f) * 255), hsv));
        p.setBlendMode(kModes[std::clamp(d.blend, 0, 3)]);
        if (d.stroke > 0) {
            p.setStyle(SkPaint::kStroke_Style);
            p.setStrokeWidth(d.stroke * k);
        }
        const float w = d.w * k / 2, h = d.h * k / 2;
        c->save();
        c->translate(st.left() + d.x * k, st.top() + d.y * k);
        c->rotate(d.rotation);
        c->drawPath(shapePath(d.kind, w, h), p);
        if (selected) {
            SkPaint o;
            o.setAntiAlias(true);
            o.setStyle(SkPaint::kStroke_Style);
            o.setStrokeWidth(1.5f);
            o.setColor(kSelect);
            c->drawRect(SkRect::MakeLTRB(-w - 3, -h - 3, w + 3, h + 3), o);
        }
        c->restore();
    }
    Studio& m_s;
    Label* m_info;
};


// ---------------------------------------------------------------- status bar

// Scene stats on the left, view in the middle, frame stats on the right. The frame stats come from
// Window::setOnFrame and refresh at most twice a second: the refresh is itself a frame, so updating on
// every frame would keep the window rendering forever. Idle, nothing renders and the last values stay.
class StatusBar : public Panel {
public:
    explicit StatusBar(Studio& s) : Panel(SkColorSetRGB(24, 24, 26)), m_s(s) {
        direction(Direction::Row).height(24).padding(Edge::Horizontal, 10).gap(24).alignItems(Align::Center);
        m_scene = &add<Label>("").fontSize(11).color(kMuted);
        m_view = &add<Label>("").fontSize(11).color(kMuted).grow(1);
        m_frame = &add<Label>("").fontSize(11).color(kMuted);
    }
    void sceneChanged(bool structure = false) {
        if (structure || !m_widgets) m_widgets = count(*window()->root());
        int links = 0, keys = 0, selected = 0, shapes = 0;
        for (auto& n : m_s.doc.nodes) {
            for (Node* in : n->inputs) links += in != nullptr;
            for (const Param& p : n->params) keys += int(p.keys.size());
            selected += n->selected;
            shapes += n->type == NodeType::Output;
        }
        set(m_scene, fmt("%zu nodes   %d links   %d keyframes   %d selected   %d shapes", m_s.doc.nodes.size(), links, keys, selected, shapes));
    }
    void viewChanged() {
        set(m_view, fmt("zoom %d%%   %s   frame %d", int(std::round(m_s.editor->zoom() * 100)), m_s.editor->lod() ? "LOD boxes" : m_s.editor->lodEnabled() ? "full nodes" : "full nodes (LOD off)",
                        int(std::round(m_s.time * kFps))));
    }
    void onFrame() {
        const FrameStats& f = window()->lastFrameStats();
        const auto now = Clock::now();
        m_times.push_back(now);
        while (now - m_times.front() > std::chrono::seconds(1)) m_times.erase(m_times.begin());
        m_cpu += f.cpuMs, m_layout += f.layoutMs, m_paint += f.paintMs, m_painted += f.widgetsPainted, ++m_frames;
        if (now - m_shown < std::chrono::milliseconds(500)) return;
        m_shown = now;
        const double n = m_frames;
        set(m_frame, fmt("%zu fps   cpu %.2f ms   layout %.2f   paint %.2f   %.0f of %d widgets painted", m_times.size(),
                         m_cpu / n, m_layout / n, m_paint / n, m_painted / n, m_widgets));
        m_cpu = m_layout = m_paint = m_painted = 0;
        m_frames = 0;
    }

private:
    using Clock = std::chrono::steady_clock;
    template <class... A> static std::string fmt(const char* f, A... a) {
        char b[200];
        std::snprintf(b, sizeof b, f, a...);
        return b;
    }
    static void set(Label* l, const std::string& t) {
        if (l->text() != t) l->text(t);
    }
    static int count(const Widget& w) {
        int n = 1;
        for (auto& c : w.children()) n += count(*c);
        return n;
    }
    Studio& m_s;
    Label *m_scene, *m_view, *m_frame;
    std::vector<Clock::time_point> m_times; // presents in the last second
    Clock::time_point m_shown{};
    double m_cpu = 0, m_layout = 0, m_paint = 0, m_painted = 0;
    int m_frames = 0, m_widgets = 0;
};

// ---------------------------------------------------------------- Studio

void Studio::setTime(float t, bool fromPlayback) {
    if (!fromPlayback && playing) togglePlay();
    t = std::clamp(t, 0.0f, kDuration);
    if (t == time) return;
    const float old = time;
    time = t;
    editor->refreshValues(false);
    preview->markPaintDirty();
    preview->updateInfo();
    timeline->timeMoved(old);
    status->viewChanged();
}

void Studio::togglePlay() {
    playing = !playing;
    if (playing) timeline->play();
    else timeline->stop();
}

void Studio::editParam(Node* n, int p, float v) {
    Param& prm = n->params[p];
    if (prm.animated()) prm.setKey(snapFrame(time), v);
    else prm.value = v;
    if (n->view) n->view->refreshParam(p);
    preview->markPaintDirty();
    if (prm.animated()) timeline->repaint();
}

void Studio::toggleKey(Node* n, int p) {
    Param& prm = n->params[p];
    const float t = snapFrame(time);
    if (prm.keyAt(t)) prm.removeKey(t);
    else prm.setKey(t, n->at(p, time));
    keysChanged();
}

void Studio::insertKeys() {
    const float t = snapFrame(time);
    for (Node* n : doc.selection())
        for (int p = 0; p < int(n->params.size()); ++p) n->params[p].setKey(t, n->at(p, time));
    keysChanged();
}

void Studio::deleteSelectedKeys() {
    for (auto& [n, t] : selectedKeys)
        for (Param& p : n->params) p.removeKey(t);
    selectedKeys.clear();
    keysChanged();
}

void Studio::keysChanged(const std::vector<Node*>& touched) {
    if (touched.empty()) editor->refreshValues(true);
    for (Node* n : touched) {
        if (n->view->isVisible()) n->view->refresh(true);
        else n->view->stale = true;
    }
    preview->markPaintDirty();
    timeline->repaint();
    status->sceneChanged();
}

void Studio::select(Node* n, int mods, bool frame) {
    if (mods & ModShift) n->selected = !n->selected;
    else doc.selectOnly(n);
    selectionChanged();
    if (frame) editor->frame(n);
}

void Studio::selectionChanged() {
    editor->markPaintDirty();
    list->refresh();
    timeline->repaint();
    preview->markPaintDirty();
    status->sceneChanged();
}

void Studio::graphChanged() {
    editor->markPaintDirty();
    preview->markPaintDirty();
    status->sceneChanged();
}

void Studio::structureChanged() {
    list->rebuild();
    timeline->rebuild();
    editor->markPaintDirty();
    preview->markPaintDirty();
    preview->updateInfo();
    status->sceneChanged(true);
}

void Studio::addNode(NodeType t, SkPoint pos) {
    doc.selectOnly(nullptr);
    Node* n = doc.add(t, pos);
    n->selected = true;
    editor->addView(n);
    editor->updateVisibility();
    structureChanged();
    selectionChanged();
}

void Studio::deleteSelected() {
    Overlay::of(*editor).close(); // an open popup may be editing one of these nodes
    std::erase_if(selectedKeys, [](const KeyRef& k) { return k.first->selected; });
    for (auto& n : doc.nodes)
        if (n->selected) editor->removeView(n.get());
    doc.removeSelected();
    structureChanged();
    selectionChanged();
}

void Studio::paste(const Clip& c, SkVector offset) {
    for (Node* n : doc.paste(c, offset)) editor->addView(n);
    editor->updateVisibility();
    structureChanged();
    selectionChanged();
}

bool Studio::handleKey(const KeyEvent& e, Widget& from) {
    if (e.action == KeyAction::Release) return false;
    if (Window* w = from.window(); w && dynamic_cast<InputText*>(w->focusedWidget())) return false; // typing
    const bool cmd = e.mods & (ModCtrl | ModSuper), shift = e.mods & ModShift;
    switch (e.key) {
    case Key::Space: togglePlay(); return true;
    case Key::Home: setTime(0); return true;
    case Key::End: setTime(kDuration); return true;
    case Key::Left:
    case Key::Right: setTime(snapFrame(time + (shift ? 1.0f : 1.0f / kFps) * (e.key == Key::Left ? -1 : 1))); return true;
    case Key::K: insertKeys(); return true;
    case Key::F: editor->frameSelection(); return true;
    case Key::Escape: doc.selectOnly(nullptr); selectionChanged(); return true;
    case Key::Delete:
    case Key::Backspace: deleteSelected(); return true;
    default: break;
    }
    if (!cmd) return false;
    switch (e.key) {
    case Key::A:
        for (auto& n : doc.nodes) n->selected = true;
        selectionChanged();
        return true;
    case Key::C: clipboard = doc.copySelected(); return true;
    case Key::X:
        clipboard = doc.copySelected();
        deleteSelected();
        return true;
    case Key::D: paste(doc.copySelected(), {30, 30}); return true;
    case Key::V: {
        if (clipboard.items.empty()) return true;
        SkVector off{40, 40};
        if (auto m = editor->mouseContent()) off = *m - clipboard.topLeft();
        paste(clipboard, off);
        return true;
    }
    default: return false;
    }
}

// ---------------------------------------------------------------- --bench

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

struct Series {
    std::vector<double> v;
    double at(double q) const {
        if (v.empty()) return 0;
        auto s = v;
        std::sort(s.begin(), s.end());
        return s[std::min(s.size() - 1, size_t(std::max(0.0, std::ceil(q * s.size()) - 1)))];
    }
};

int countWidgets(const Widget& w) {
    int n = 1;
    for (auto& c : w.children()) n += countWidgets(*c);
    return n;
}

// Scripted interactions through the real code paths (Studio, widgets, injected mouse events), each
// step followed by one Window::renderFrame(). Timings come from FrameStats, as in app/benchmark.cpp.
void runBench(Application& app, Window* win, Studio& s, DockNode* centerSplit) {
    for (int i = 0; i < 400 && win->lastFrameStats().frameIndex == 0; ++i) {
        app.pumpOnce(false);
        win->renderFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto drain = [&] {
        for (int i = 0; i < 8 && win->needsFrame(); ++i) win->renderFrame();
    };
    drain();
    const double winArea = win->size().width() * win->size().height();
    std::printf("\n%-38s %6s %15s %15s %15s %8s %8s %6s\n", "scenario (median / p95 ms)", "frames", "cpu", "layout", "paint", "mut",
                "widgets", "dmg%");
    auto run = [&](const char* name, int steps, const std::function<void(int)>& step) {
        Series cpu, layout, paint, mut;
        double widgets = 0, dmg = 0;
        for (int i = 0; i < steps; ++i) {
            const auto t0 = Clock::now();
            step(i);
            mut.v.push_back(msSince(t0));
            if (win->renderFrame()) {
                const FrameStats& f = win->lastFrameStats();
                cpu.v.push_back(f.cpuMs);
                layout.v.push_back(f.layoutMs);
                paint.v.push_back(f.paintMs);
                widgets += f.widgetsPainted;
                dmg += f.damageArea;
            }
            drain();
            if (i % 16 == 15) app.pumpOnce(false);
        }
        const size_t n = std::max<size_t>(1, cpu.v.size());
        auto pair = [](const Series& x) {
            char b[32];
            std::snprintf(b, sizeof b, "%6.2f /%6.2f", x.at(0.5), x.at(0.95));
            return std::string(b);
        };
        std::printf("%-38s %6zu %15s %15s %15s %8.2f %8.0f %6.1f\n", name, cpu.v.size(), pair(cpu).c_str(), pair(layout).c_str(),
                    pair(paint).c_str(), mut.at(0.5), widgets / n, dmg / n / winArea * 100);
    };

    run("playback, 1 frame per step", 300, [&](int i) { s.setTime((i + 1) / kFps, true); });
    s.setTime(0);
    drain();

    const SkPoint pan0 = s.editor->pan();
    run("pan node editor", 240, [&](int i) { s.editor->setPan(pan0 + SkVector{i * 9.0f, i * 5.0f}); });
    s.editor->setPan(pan0);
    drain();
    run("zoom out to overview (x0.95 per step)", 60, [&](int) { s.editor->zoomBy(0.95f); });
    s.editor->frameAll();
    drain();
    const SkPoint all0 = s.editor->pan();
    run("pan overview (all 2000 nodes)", 120, [&](int i) { s.editor->setPan(all0 + SkVector{i * 20.0f, i * 12.0f}); });
    s.editor->setLodEnabled(false); // every node as full widgets
    s.editor->setPan(all0);
    drain();
    run("pan overview, LOD off (full widgets)", 60, [&](int i) { s.editor->setPan(all0 + SkVector{i * 20.0f, i * 12.0f}); });
    s.editor->setLodEnabled(true);
    s.editor->setView(pan0, 0.36f);
    drain();
    run("zoom in from 36% (x1.05 per step)", 40, [&](int) { s.editor->zoomBy(1.05f); });
    s.editor->setView(pan0, 1);
    drain();

    Node* first = s.doc.nodes.front().get();
    s.editor->frame(first);
    drain();
    { // drag the first node by its title with injected mouse events
        const SkRect b = first->view->mapToWindow(first->view->bounds());
        float x = b.left() + 40, y = b.top() + kTitleH / 2;
        win->injectMouseButton(x, y, MouseButton::Left, true);
        run("drag a node (mouse)", 120, [&](int i) { win->injectMouseMove(x += (i < 60 ? 2.0f : -2.0f), y += 1.0f); });
        win->injectMouseButton(x, y, MouseButton::Left, false);
        drain();
    }
    { // drag a number field: edits the parameter, re-evaluates the preview
        Node* pos = s.doc.nodes[1].get(); // Position 1
        s.editor->frame(pos);
        drain();
        DragNumber* field = pos->view->numberField(kY);
        const SkRect b = field->mapToWindow(field->bounds());
        float x = b.centerX();
        win->injectMouseButton(x, b.centerY(), MouseButton::Left, true);
        run("drag a number field (mouse)", 120, [&](int i) { win->injectMouseMove(x += (i < 60 ? 1.0f : -1.0f), b.centerY()); });
        win->injectMouseButton(x, b.centerY(), MouseButton::Left, false);
        drain();
    }
    { // pick up the wire at Output 1's Shape input and drop it back
        Node* out = s.doc.nodes[4].get();
        s.editor->frame(out);
        drain();
        const SkRect b = out->view->mapToWindow(out->view->bounds());
        const SkPoint port{b.left() + inputPort(*out, kInShape).x(), b.top() + inputPort(*out, kInShape).y()};
        win->injectMouseButton(port.x(), port.y(), MouseButton::Left, true);
        run("drag a wire (mouse)", 120, [&](int i) {
            const float k = float(i < 60 ? i : 119 - i);
            win->injectMouseMove(port.x() - k * 3, port.y() + k);
        });
        win->injectMouseButton(port.x(), port.y(), MouseButton::Left, false);
        drain();
    }
    { // box select from the empty space left of the first group
        s.editor->setPan({-300, -40});
        drain();
        const SkRect eb = s.editor->bounds();
        const float x0 = eb.left() + 20, y0 = eb.top() + 20;
        win->injectMouseButton(x0, y0, MouseButton::Left, true);
        run("box select (mouse)", 120, [&](int i) { win->injectMouseMove(x0 + i * 5.0f, y0 + i * 3.0f); });
        win->injectMouseButton(x0 + 600, y0 + 360, MouseButton::Left, false);
        drain();
    }
    run("open / close context menu (mouse)", 40, [&](int i) {
        const SkRect eb = s.editor->bounds();
        const float x = eb.left() + 30, y = eb.top() + 30;
        const MouseButton b = i % 2 ? MouseButton::Left : MouseButton::Right; // a left press outside closes it
        win->injectMouseButton(i % 2 ? x + 400 : x, y, b, true);
        win->injectMouseButton(i % 2 ? x + 400 : x, y, b, false);
        // The menu is its own OS window: its creation and first frame count as this step's work ("mut").
        if (Window* popup = Overlay::of(*s.editor).popupWindow()) popup->renderFrame();
    });
    { // drag Position 1's keys along its timeline track
        const SkRect lane = s.timeline->tracks().laneRect();
        const float y = lane.top() + 1.5f * kTrackH, x0 = laneX(lane, 5);
        win->injectMouseButton(x0, y, MouseButton::Left, true);
        run("drag a keyframe (mouse)", 120, [&](int i) { win->injectMouseMove(x0 + (i < 60 ? i : 119 - i) * 2.0f, y); });
        win->injectMouseButton(x0, y, MouseButton::Left, false);
        drain();
    }
    run("scroll node list", 240, [&](int i) { s.list->onScroll(0, i < 120 ? -17.0f : 17.0f); });
    run("scroll timeline tracks", 240, [&](int i) { s.timeline->tracks().onScroll(0, i < 120 ? -17.0f : 17.0f); });
    run("select all / none", 20, [&](int i) {
        for (auto& n : s.doc.nodes) n->selected = i % 2 == 0;
        s.selectionChanged();
    });
    s.doc.selectOnly(nullptr);
    run("duplicate 5 nodes / delete them", 40, [&](int i) {
        if (i % 2 == 0) {
            for (int k = 0; k < 5; ++k) s.doc.nodes[k]->selected = true;
            s.paste(s.doc.copySelected(), {30, 30});
        } else {
            s.deleteSelected();
        }
    });
    const float ratio = centerSplit->ratio();
    run("resize editor / timeline split", 60, [&](int i) { centerSplit->setRatio(ratio + (i < 30 ? i : 60 - i) * 0.004f); });
    centerSplit->setRatio(ratio);
    drain();
}

} // namespace NodeEd

using namespace NodeEd;

int main(int argc, char** argv) {
    bool bench = false, interactive = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--bench")) bench = true;
        else if (!std::strcmp(argv[i], "--interactive")) interactive = true;
    }
    try {
        Studio s; // outlives the Application, so widgets never see a dead Studio
        Application app;
        Window* window = app.createWindow({.title = "Terracotta2 - Node Editor", .width = 1720, .height = 980});

        const auto t0 = Clock::now();
        buildDemo(s.doc, 400);
        DockSpace& dock = window->root()->add<DockSpace>();
        auto [left, rest] = dock.splitHorizontal(0.16f);
        auto [center, right] = rest->splitHorizontal(0.70f);
        auto [top, bottom] = center->splitVertical(0.64f);
        left->setTabTitle("Nodes");
        left->setContent<NodeListPanel>(s);
        top->setTabTitle("Node Editor");
        top->setContent<EditorPanel>(s);
        bottom->setTabTitle("Timeline");
        s.timeline = &bottom->setContent<TimelinePanel>(s);
        right->setTabTitle("Preview");
        s.preview = &right->setContent<Preview>(s);
        s.status = &window->root()->add<StatusBar>(s);
        s.editor->onViewChanged = [&s] { s.status->viewChanged(); };
        window->setOnFrame([&s] { s.status->onFrame(); });
        for (auto& n : s.doc.nodes) s.editor->addView(n.get());
        s.editor->setPan({-40, -40});
        s.structureChanged();
        s.status->viewChanged();
        std::printf("built %zu nodes, %d widgets in %.0f ms\n", s.doc.nodes.size(), countWidgets(*window->root()), msSince(t0));

        if (bench) {
            runBench(app, window, s, center);
            if (!interactive) return 0;
        }
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return -1;
    }
    return 0;
}
