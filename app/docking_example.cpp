// Terracotta2 showcase: a docked workspace that doubles as a layout + text + custom-drawing tutorial.
//
// DECLARING UI (read this first)
//  * Every Widget is a flexbox container (Yoga). Children are laid out along the container's
//    main axis: a COLUMN stacks them top-to-bottom, a ROW left-to-right.
//    VBox = column, HBox = row, Panel = column with a background colour.
//  * parent.add<T>(args...) constructs a T (args go to T's constructor), makes it a child of
//    `parent` and returns a T&. Setters are chainable and return the widget's own type, and
//    build(fn) runs fn on the widget, so the code is shaped like the tree it creates:
//
//        page.add<HBox>().gap(12).alignItems(Align::Center).build([](HBox& row) {
//            row.add<Label>("Name").fontSize(11).color(kMuted).width(110);
//            row.add<InputText>("Enter value...").onChange([](const std::string&) {}).grow(1);
//        });
//
//    Keep the reference when you need the widget later: `Label& status = row.add<Label>("");`
//    (and capture `&status` in a lambda, never the local reference variable itself).
//  * Sizing: width/height/size = fixed. grow(n) = "take n shares of the free space along the
//    main axis". Children with no size are as big as their content.
//  * Spacing: padding = space INSIDE a widget, gap = space BETWEEN its children,
//    margin = space OUTSIDE a widget.
//  * Alignment: justify places children along the main axis, alignItems along the cross axis
//    (default: stretch), alignSelf overrides it for one child.
//  * A Label measures itself and WRAPS to the width its parent gives it. Put one in a column
//    and it becomes a paragraph; cap it with maxWidth(680) for a readable line
//    length; put two in a row with grow(1) and you get text columns.
//  * Content taller than its container? Put it in a ScrollView (children keep their natural
//    height; wheel / trackpad / drag the thumb).
//  * Fonts: label.font("Georgia") picks an installed family, loadFont(path) registers a font
//    file, theme().fontFamily changes the app-wide default (see the Typography tab).
//  * Custom drawing: Canvas (a draw lambda) or subclass Widget and override onDraw / input
//    handlers. Leaving Yoga: absolute(x, y) for one overlaid child, manualLayout() when the
//    parent places ALL its children itself (see the Custom tab).
//  * Docking: drag a tab onto a pane's centre (merge) or edge (split). Drag any tab outside
//    the window to float it; drag a floating window's tab back into the main window to dock.
#include <terracotta2/Terracotta2.hpp>
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkGradient.h"
#include <algorithm>
#include <numbers>
#include <cmath>
#include <cstdio>
#include <iostream>

using namespace Terracotta2;

namespace {

constexpr SkColor kText = SkColorSetRGB(225, 225, 225);
constexpr SkColor kMuted = SkColorSetRGB(135, 135, 140);
constexpr SkColor kPage = SkColorSetRGB(30, 30, 32);
constexpr SkColor kArea = SkColorSetRGB(22, 22, 24);
constexpr SkColor kPalette[] = {
    SkColorSetRGB(214, 90, 70),  SkColorSetRGB(230, 160, 60), SkColorSetRGB(110, 180, 90),
    SkColorSetRGB(60, 160, 190), SkColorSetRGB(100, 120, 210), SkColorSetRGB(170, 100, 200),
};
constexpr float kAuto = NAN; // "no explicit size" for box()

// Coloured rectangle. w/h = kAuto: size comes from content / the parent's alignment.
Panel& box(Widget& parent, SkColor c, float w, float h, const std::string& caption = {}) {
    Panel& p = parent.add<Panel>(c);
    if (!std::isnan(w)) p.width(w);
    if (!std::isnan(h)) p.height(h);
    if (!caption.empty())
        p.alignItems(Align::Center).justify(Justify::Center).build([&](Panel& b) {
            b.add<Label>(caption).fontSize(11).weight(600).color(SK_ColorWHITE);
        });
    return p;
}

// A section heading with a muted one-line explanation under it.
void header(Widget& parent, const std::string& title, const std::string& note) {
    parent.add<Label>(title).fontSize(17).weight(600);
    parent.add<Label>(note).fontSize(12).color(kMuted);
}

// One lesson in the Layout gallery: a title, the exact calls used, and a fixed-height
// container to put the demo children in. `fill` receives that container (a row by default;
// call d.direction(...) to change it).
template <class F>
void demo(Widget& page, const char* title, const std::string& calls, float height, F&& fill) {
    page.add<VBox>().gap(3).build([&](VBox& group) {
        group.add<Label>(title).fontSize(13).weight(600);
        group.add<Label>(calls).fontSize(11).color(kMuted);
        group.add<Panel>(kArea).height(height).direction(Direction::Row).padding(8).gap(8).build(fill);
    });
}

// Scrollable page with a background, padding and a gap between its children.
class Page : public ScrollView {
public:
    Page() {
        padding(16).padding(Edge::Right, 22).gap(14); // 22: keep content clear of the scrollbar
    }
    void onDraw(SkCanvas* canvas) override {
        SkPaint p;
        p.setColor(kPage);
        canvas->drawRect(m_bounds, p);
    }
};

const char* kLorem[] = {
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et "
    "dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip "
    "ex ea commodo consequat.",
    "Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. "
    "Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est "
    "laborum. Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium.",
    "Nemo enim ipsam voluptatem quia voluptas sit aspernatur aut odit aut fugit, sed quia consequuntur magni "
    "dolores eos qui ratione voluptatem sequi nesciunt. Neque porro quisquam est, qui dolorem ipsum quia dolor "
    "sit amet, consectetur, adipisci velit.",
    "At vero eos et accusamus et iusto odio dignissimos ducimus qui blanditiis praesentium voluptatum deleniti "
    "atque corrupti quos dolores et quas molestias excepturi sint occaecati cupiditate non provident, similique "
    "sunt in culpa qui officia deserunt mollitia animi.",
};

// ---------------------------------------------------------------- Typography

class TypographyPage : public Page {
public:
    TypographyPage() {
        add<Label>("Typography").fontSize(26).weight(700);
        add<Label>("Every sample below is just add<Label>(text).fontSize(px).weight(100..900)  (400 regular, 700 bold).")
            .fontSize(12).color(kMuted);

        // --- type scale: a ROW per sample = [fixed-width caption][sample] ---
        header(*this, "Type scale", "Row = HBox().gap(14).alignItems(Align::Center); the caption has width(110) so samples line up.");
        struct Step { const char* name; float size; int weight; };
        const Step scale[] = {{"Display", 32, 700}, {"H1", 26, 700}, {"H2", 21, 600}, {"H3", 17, 600},
                              {"Body", 14, 400},    {"Small", 12, 400}, {"Caption", 11, 400}};
        for (const Step& s : scale)
            add<HBox>().gap(14).alignItems(Align::Center).build([&](HBox& row) {
                row.add<Label>(std::string(s.name) + "  " + std::to_string(int(s.size)) + " / " + std::to_string(s.weight))
                    .fontSize(11).color(kMuted).width(110);
                row.add<Label>("The quick brown fox jumps over the lazy dog")
                    .fontSize(s.size).weight(s.weight).color(s.size <= 11 ? kMuted : kText);
            });

        // --- fonts ---
        header(*this, "Fonts",
               "label.font(\"Family\") uses any installed family; loadFont(path) registers a font file and returns its family name.");
        const char* sentence = "Sphinx of black quartz, judge my vow";
        add<VBox>().gap(6).build([&](VBox& col) {
            auto sample = [&](const std::string& caption, auto&& configure) {
                col.add<HBox>().gap(14).alignItems(Align::Center).build([&](HBox& row) {
                    row.add<Label>(caption).fontSize(11).color(kMuted).width(110);
                    configure(row);
                });
            };
            sample("default", [&](HBox& row) { row.add<Label>(sentence).fontSize(20); });
            for (const char* family : {"Georgia", "Menlo", "Avenir Next", "Snell Roundhand"})
                sample(family, [&](HBox& row) { row.add<Label>(sentence).fontSize(20).font(family); });

            // A font loaded from a file: returns "" if it can't be loaded.
            std::string chalk = loadFont("/System/Library/Fonts/Supplemental/Chalkduster.ttf");
            sample("loadFont(file)", [&](HBox& row) {
                if (chalk.empty()) row.add<Label>("font file not found").fontSize(12).color(kMuted);
                else row.add<Label>(sentence).fontSize(20).font(chalk);
            });
        });
        add<Label>("App-wide default (set it BEFORE building any UI):\n"
                   "    theme().fontFamily = \"Georgia\";                      // an installed family\n"
                   "    theme().fontFamily = loadFont(\"/path/MyFont.ttf\");   // or your own font file")
            .fontSize(11).color(kMuted).lineHeight(1.4f);

        // --- paragraphs in a column: Labels wrap to the width they are given ---
        header(*this, "Paragraphs", "Body 14px, lineHeight(1.45), maxWidth(680). Resize the window or the dock pane: the lines re-wrap.");
        for (int i = 0; i < 4; ++i)
            add<Label>(kLorem[i]).fontSize(14).lineHeight(1.45f).maxWidth(680);

        // --- two text columns: a row of Labels, each grow(1) ---
        header(*this, "Two columns", "HBox().gap(16) + two Labels with grow(1).basis(0): equal widths, each wraps on its own.");
        add<HBox>().gap(16).build([](HBox& cols) {
            for (int i = 0; i < 2; ++i) cols.add<Label>(kLorem[i]).fontSize(13).lineHeight(1.4f).grow(1).basis(0);
        });

        // --- narrow columns ---
        header(*this, "Narrow columns and long words",
               "A Label wraps at word boundaries, so a narrow column gets ragged quickly. A single word wider than the column is "
               "broken or clipped rather than reflowed: give the column more room (minSize / grow) when that matters.");
        add<HBox>().gap(12).build([](HBox& narrow) {
            for (float w : {90.0f, 140.0f, 220.0f})
                narrow.add<Panel>(kArea).width(w).padding(8).build([](Panel& col) {
                    col.add<Label>("Supercalifragilistic words wrap here.").fontSize(12);
                });
        });
    }
};

// ---------------------------------------------------------------- Layout gallery

class LayoutPage : public Page {
public:
    LayoutPage() {
        add<Label>("Layout").fontSize(26).weight(700);
        add<Label>("Each box shows one idea. The grey line under a title lists the exact calls used.").fontSize(12).color(kMuted);

        // 1. direction
        demo(*this, "Row", "direction(Direction::Row)  - children go left to right", 56, [](Panel& d) {
            for (int i = 0; i < 3; ++i) box(d, kPalette[i], 70, 36, std::to_string(i + 1));
        });
        demo(*this, "Column", "direction(Direction::Column)  - children go top to bottom", 150, [](Panel& d) {
            d.direction(Direction::Column);
            for (int i = 0; i < 3; ++i) box(d, kPalette[i], 120, 30, std::to_string(i + 1));
        });

        // 2. justify: distribution along the MAIN axis (here: horizontal)
        struct J { const char* name; Justify v; };
        for (J j : {J{"Justify::Start", Justify::Start}, J{"Justify::Center", Justify::Center},
                    J{"Justify::End", Justify::End}, J{"Justify::SpaceBetween", Justify::SpaceBetween},
                    J{"Justify::SpaceAround", Justify::SpaceAround}, J{"Justify::SpaceEvenly", Justify::SpaceEvenly}})
            demo(*this, "justify  (main axis)", std::string("justify(") + j.name + ")", 52, [&](Panel& d) {
                d.justify(j.v);
                for (int i = 0; i < 3; ++i) box(d, kPalette[i], 56, 34);
            });

        // 3. alignItems: placement on the CROSS axis (here: vertical). Children differ in height.
        struct A { const char* name; Align v; };
        for (A a : {A{"Align::Start", Align::Start}, A{"Align::Center", Align::Center},
                    A{"Align::End", Align::End}, A{"Align::Stretch", Align::Stretch}}) {
            std::string note = std::string("alignItems(") + a.name + ")";
            if (a.v == Align::Stretch) note += "  - children without a height fill the row";
            demo(*this, "alignItems  (cross axis)", note, 92, [&](Panel& d) {
                d.alignItems(a.v);
                const float heights[] = {24, 44, 64};
                for (int i = 0; i < 3; ++i) box(d, kPalette[i + 2], 56, a.v == Align::Stretch ? kAuto : heights[i]);
            });
        }

        // 4. grow: share the free space
        demo(*this, "grow 1 : 2 : 3", "each child: basis(0).grow(n)  - widths are in ratio 1:2:3", 56, [](Panel& d) {
            for (int i = 1; i <= 3; ++i)
                box(d, kPalette[i], kAuto, 40, "grow " + std::to_string(i)).basis(0).grow(float(i));
        });
        demo(*this, "Fixed + flexible", "left: width(80)   middle: grow(1)   right: width(80)", 56, [](Panel& d) {
            box(d, kPalette[0], 80, 40, "80px");
            box(d, kPalette[3], kAuto, 40, "fills the rest").grow(1);
            box(d, kPalette[0], 80, 40, "80px");
        });

        // 5. wrapping
        demo(*this, "wrap", "wrap() + gap(8) with 16 fixed 60x28 chips - they flow onto new lines", 150,
             [](Panel& d) {
                 d.wrap();
                 for (int i = 0; i < 16; ++i) box(d, kPalette[i % 6], 60, 28, std::to_string(i + 1));
             });

        // 6. padding / gap / margin
        demo(*this, "padding, gap, margin", "container: padding(24).gap(16)   middle box: margin(10)", 120, [](Panel& d) {
            d.padding(24).gap(16);
            box(d, kPalette[0], 60, 60, "A");
            box(d, kPalette[1], 60, 60, "B").margin(10);
            box(d, kPalette[2], 60, 60, "C");
        });

        // 7. alignSelf
        demo(*this, "alignSelf",
             "container: alignItems(Align::Start); 2nd child: alignSelf(Align::Center), 3rd: Align::End", 100,
             [](Panel& d) {
                 d.alignItems(Align::Start);
                 box(d, kPalette[0], 64, 30, "start");
                 box(d, kPalette[1], 64, 30, "center").alignSelf(Align::Center);
                 box(d, kPalette[2], 64, 30, "end").alignSelf(Align::End);
             });

        // 8. nesting: a column holding a toolbar, a row (sidebar | content) and a status bar
        demo(*this, "Nesting: toolbar + sidebar + content + status bar",
             "column [ toolbar 28px | row [ sidebar 90px | content grow(1) ] (grow 1) | status 18px ]", 220, [](Panel& d) {
                 d.direction(Direction::Column).padding(0).gap(2);
                 box(d, kPalette[4], kAuto, 28, "toolbar (height 28)");
                 d.add<HBox>().grow(1).gap(2).build([](HBox& body) {
                     box(body, kPalette[3], 90, kAuto, "sidebar").alignItems(Align::Center);
                     box(body, kPalette[5], kAuto, kAuto, "content (grow 1)").grow(1);
                 });
                 box(d, kPalette[2], kAuto, 18, "status (height 18)");
             });
    }
};

// ---------------------------------------------------------------- Custom drawing, input and layout

// ---- 2. Subclassing for drawing AND input: a rotary knob. Drag up/down to turn it (Shift = fine).
// onDraw paints, onMouse* edit state, markPaintDirty() repaints just this widget (no layout).
// The widget that handles onMouseDown captures the mouse, so the drag keeps working outside it.
class Knob : public Widget {
public:
    Knob(SkColor accent, float value) : m_accent(accent), m_value(value) { size(64, 64); }

    Knob& onChange(std::function<void(float)> fn) { m_onChange = std::move(fn); return *this; }

    bool onMouseDown(const MouseEvent& e) override { m_lastY = e.y; m_drag = true; markPaintDirty(); return true; }
    bool onMouseMove(const MouseEvent& e) override {
        if (!m_drag) return false;
        float perPixel = (e.mods & ModShift) ? 1.0f / 800 : 1.0f / 150;
        m_value = std::clamp(m_value + (m_lastY - e.y) * perPixel, 0.0f, 1.0f);
        m_lastY = e.y;
        markPaintDirty();
        if (m_onChange) m_onChange(m_value);
        return true;
    }
    bool onMouseUp(const MouseEvent&) override { m_drag = false; markPaintDirty(); return true; }
    void onMouseEnter() override { m_hover = true; markPaintDirty(); }
    void onMouseLeave() override { m_hover = false; markPaintDirty(); }
    Cursor cursor() const override { return Cursor::Hand; }

    void onDraw(SkCanvas* c) override {
        SkPoint mid{m_bounds.centerX(), m_bounds.centerY()};
        float r = std::min(m_bounds.width(), m_bounds.height()) / 2 - 6;
        SkRect oval = SkRect::MakeLTRB(mid.x() - r, mid.y() - r, mid.x() + r, mid.y() + r);
        constexpr float kStart = 135, kSweep = 270; // degrees; 0 = 3 o'clock, clockwise (y is down)

        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(m_drag || m_hover ? SkColorSetRGB(48, 48, 52) : SkColorSetRGB(40, 40, 44)); // body
        c->drawCircle(mid, r - 5, p);
        p.setStyle(SkPaint::kStroke_Style);
        p.setStrokeWidth(4);
        p.setStrokeCap(SkPaint::kRound_Cap);
        p.setColor(SkColorSetRGB(60, 60, 66)); // track
        c->drawArc(oval, kStart, kSweep, false, p);
        p.setColor(m_accent); // value
        c->drawArc(oval, kStart, kSweep * m_value, false, p);
        float a = (kStart + kSweep * m_value) * std::numbers::pi_v<float> / 180;
        p.setStrokeWidth(2.5f);
        p.setColor(SK_ColorWHITE); // pointer
        c->drawLine(mid.x() + std::cos(a) * (r - 14), mid.y() + std::sin(a) * (r - 14),
                    mid.x() + std::cos(a) * (r - 6), mid.y() + std::sin(a) * (r - 6), p);
    }

private:
    SkColor m_accent;
    float m_value, m_lastY = 0;
    bool m_drag = false, m_hover = false;
    std::function<void(float)> m_onChange;
};

// ---- 3. Manual layout: a mini node graph.
//
// WHEN TO USE WHAT
//  * Yoga (default): UI that reflows - toolbars, forms, panels. The parent decides sizes/positions.
//  * absolute(x, y): ONE child floats over its parent's content (badge, overlay, drop target)
//    while its siblings still flow normally. Still sized by Yoga.
//  * manualLayout(): the parent places ALL its children itself with child.setFrame(rect) - node
//    graphs, timelines, free canvases, virtualized lists. Moving a child is one setFrame call:
//    no Yoga pass over siblings, and only that child (old + new rect) is damaged. Each child
//    still lays out its OWN contents with Yoga inside the frame you give it.

struct NodeRow {
    std::string name, value;
    bool in = false, out = false; // draws a port on the left / right edge of the row
};

class NodeGraph;

// Dragging the title moves the whole node (NodeGraph::dragNode -> setFrame).
class NodeTitle : public HBox {
public:
    NodeTitle(NodeGraph& graph, int index, const std::string& title);
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent&) override { m_drag = false; return true; }
    Cursor cursor() const override { return Cursor::Move; }

private:
    NodeGraph& m_graph;
    int m_index;
    SkPoint m_grab{};
    bool m_drag = false;
};

constexpr float kNodeW = 150, kTitleH = 24, kRowH = 22, kNodeFoot = 6, kPortInset = 9;

// A node is an ordinary VBox: [title][row][row]... Yoga lays out the inside; the graph decides where it sits.
class Node : public VBox {
public:
    Node(NodeGraph& graph, int index, const std::string& title, SkColor accent, std::vector<NodeRow> rows)
        : m_accent(accent), m_rows(std::move(rows)) {
        add<NodeTitle>(graph, index, title);
        for (const NodeRow& r : m_rows)
            add<HBox>().height(kRowH).alignItems(Align::Center).padding(Edge::Horizontal, 20).gap(6).build([&](HBox& row) {
                row.add<Label>(r.name).fontSize(11).grow(1);
                if (!r.value.empty()) row.add<Label>(r.value).fontSize(11).color(kMuted);
            });
    }
    float nodeHeight() const { return kTitleH + float(m_rows.size()) * kRowH + kNodeFoot; }

    void onDraw(SkCanvas* c) override { // before the children, so the labels land on top
        SkPaint p;
        p.setAntiAlias(true);
        SkRRect shape = SkRRect::MakeRectXY(m_bounds, 6, 6);
        p.setColor(SkColorSetRGB(44, 44, 48));
        c->drawRRect(shape, p);
        c->save();
        c->clipRRect(shape, true);
        p.setColor(m_accent);
        c->drawRect(SkRect::MakeXYWH(m_bounds.left(), m_bounds.top(), m_bounds.width(), kTitleH), p);
        c->restore();
        p.setStyle(SkPaint::kStroke_Style);
        p.setColor(SkColorSetRGB(70, 70, 76));
        c->drawRRect(SkRRect::MakeRectXY(m_bounds.makeInset(0.5f, 0.5f), 6, 6), p);
        // Ports live INSIDE the node's bounds so moving the node never leaves paint behind.
        p.setStyle(SkPaint::kFill_Style);
        for (size_t i = 0; i < m_rows.size(); ++i) {
            float y = m_bounds.top() + kTitleH + float(i) * kRowH + kRowH / 2;
            p.setColor(SkColorSetRGB(20, 20, 22));
            if (m_rows[i].in) c->drawCircle(m_bounds.left() + kPortInset, y, 4.5f, p);
            if (m_rows[i].out) c->drawCircle(m_bounds.right() - kPortInset, y, 4.5f, p);
            p.setColor(kText);
            if (m_rows[i].in) c->drawCircle(m_bounds.left() + kPortInset, y, 2.5f, p);
            if (m_rows[i].out) c->drawCircle(m_bounds.right() - kPortInset, y, 2.5f, p);
        }
    }
    SkColor accent() const { return m_accent; }

private:
    SkColor m_accent;
    std::vector<NodeRow> m_rows;
};

class NodeGraph : public Widget {
public:
    NodeGraph() { manualLayout().clipChildren(true); } // switch modes before adding children

    // Adds a node at `pos` (relative to the graph). Returns its index for connect().
    int addNode(const std::string& title, SkColor accent, SkPoint pos, std::vector<NodeRow> rows) {
        int index = int(m_nodes.size());
        Node& n = add<Node>(*this, index, title, accent, std::move(rows));
        m_nodes.push_back({&n, pos, n.nodeHeight()});
        return index;
    }
    // Wire from an output row of one node to an input row of another.
    void connect(int fromNode, int fromRow, int toNode, int toRow) { m_wires.push_back({fromNode, fromRow, toNode, toRow}); }

    SkPoint origin() const { return {m_bounds.left(), m_bounds.top()}; }
    SkPoint nodePos(int i) const { return m_nodes[i].pos; }

    // Move node i. setFrame damages the node's old + new rect; we add the wires touching it.
    void dragNode(int i, SkPoint pos) {
        Slot& s = m_nodes[i];
        pos.fX = std::clamp(pos.x(), 0.0f, std::max(0.0f, m_bounds.width() - kNodeW));
        pos.fY = std::clamp(pos.y(), 0.0f, std::max(0.0f, m_bounds.height() - s.h));
        if (pos == s.pos) return;
        invalidateWires(i);
        s.pos = pos;
        s.widget->setFrame(SkRect::MakeXYWH(pos.x(), pos.y(), kNodeW, s.h));
        invalidateWires(i);
    }

    // Manual layout: this is where children get their frames (called when our bounds change).
    void onLayout() override {
        for (const Slot& s : m_nodes) s.widget->setFrame(SkRect::MakeXYWH(s.pos.x(), s.pos.y(), kNodeW, s.h));
    }

    // Runs BEFORE the children are painted: background and wires end up underneath the nodes.
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setColor(kArea);
        c->drawRect(m_bounds, p);
        p.setColor(SkColorSetRGB(44, 44, 48)); // dot grid
        p.setStrokeWidth(2);
        for (float y = m_bounds.top() + 12; y < m_bounds.bottom(); y += 24)
            for (float x = m_bounds.left() + 12; x < m_bounds.right(); x += 24) c->drawPoint(x, y, p);

        p.setAntiAlias(true);
        p.setStyle(SkPaint::kStroke_Style);
        p.setStrokeWidth(2.5f);
        for (const Wire& w : m_wires) {
            SkPoint a = port(w.from, w.fromRow, true), b = port(w.to, w.toRow, false);
            float dx = std::max(40.0f, std::abs(b.x() - a.x()) * 0.5f);
            SkPathBuilder path;
            path.moveTo(a);
            path.cubicTo(a.x() + dx, a.y(), b.x() - dx, b.y(), b.x(), b.y());
            p.setColor(m_nodes[w.from].widget->accent());
            c->drawPath(path.detach(), p);
        }
    }

private:
    struct Slot { Node* widget; SkPoint pos; float h; };
    struct Wire { int from, fromRow, to, toRow; };

    SkPoint port(int node, int row, bool output) const { // window coordinates
        const Slot& s = m_nodes[node];
        return {m_bounds.left() + s.pos.x() + (output ? kNodeW - kPortInset : kPortInset),
                m_bounds.top() + s.pos.y() + kTitleH + float(row) * kRowH + kRowH / 2};
    }
    void invalidateWires(int node) {
        for (const Wire& w : m_wires) {
            if (w.from != node && w.to != node) continue;
            SkPoint a = port(w.from, w.fromRow, true), b = port(w.to, w.toRow, false);
            float dx = std::max(40.0f, std::abs(b.x() - a.x()) * 0.5f);
            SkRect hull = SkRect::MakeLTRB(std::min(a.x(), b.x() - dx), std::min(a.y(), b.y()),
                                           std::max(b.x(), a.x() + dx), std::max(a.y(), b.y()));
            hull.outset(4, 4); // stroke width
            invalidateRect(hull);
        }
    }

    std::vector<Slot> m_nodes;
    std::vector<Wire> m_wires;
};

NodeTitle::NodeTitle(NodeGraph& graph, int index, const std::string& title) : m_graph(graph), m_index(index) {
    height(kTitleH).alignItems(Align::Center).padding(Edge::Horizontal, 10);
    add<Label>(title).fontSize(12).weight(600).color(SK_ColorWHITE);
}
bool NodeTitle::onMouseDown(const MouseEvent& e) {
    SkPoint p = m_graph.nodePos(m_index), o = m_graph.origin();
    m_grab = {e.x - o.x() - p.x(), e.y - o.y() - p.y()}; // where in the node we grabbed it
    return m_drag = true;
}
bool NodeTitle::onMouseMove(const MouseEvent& e) {
    if (!m_drag) return false;
    SkPoint o = m_graph.origin();
    m_graph.dragNode(m_index, {e.x - o.x() - m_grab.x(), e.y - o.y() - m_grab.y()});
    return true;
}

class CustomPage : public Page {
public:
    CustomPage() {
        add<Label>("Custom").fontSize(26).weight(700);
        add<Label>("Draw anything, handle input yourself, and opt out of Yoga where it gets in the way.").fontSize(12).color(kMuted);

        // ---- 1. Canvas: custom drawing with just a lambda ----
        header(*this, "1. Canvas",
               "add<Canvas>([](SkCanvas*, const SkRect& bounds) { ... }). The button changes a parameter and calls markPaintDirty() on the "
               "canvas: only that rectangle repaints (run with TC2_SHOW_DAMAGE=1 to see the damage outline).");
        Canvas& wave = add<Canvas>([this](SkCanvas* c, const SkRect& r) {
                           c->save();
                           c->clipRRect(SkRRect::MakeRectXY(r, 6, 6), true);
                           SkColor4f stops[] = {SkColor4f::FromColor(SkColorSetRGB(38, 52, 96)),
                                                SkColor4f::FromColor(SkColorSetRGB(18, 18, 24))};
                           SkPoint line[] = {{r.left(), r.top()}, {r.left(), r.bottom()}};
                           SkPaint p;
                           p.setShader(SkShaders::LinearGradient(
                               line, SkGradient(SkGradient::Colors(stops, SkTileMode::kClamp), SkGradient::Interpolation())));
                           c->drawRect(r, p);
                           p.setShader(nullptr);
                           p.setAntiAlias(true);
                           p.setStyle(SkPaint::kStroke_Style);
                           p.setStrokeWidth(1);
                           p.setColor(SkColorSetARGB(60, 255, 255, 255));
                           c->drawLine(r.left(), r.centerY(), r.right(), r.centerY(), p); // zero line
                           // A sawtooth built from m_harmonics sine partials (Fourier series).
                           SkPathBuilder path;
                           const int steps = std::max(2, int(r.width() / 2));
                           for (int i = 0; i <= steps; ++i) {
                               float t = float(i) / steps, y = 0;
                               for (int k = 1; k <= m_harmonics; ++k) y += std::sin(2 * std::numbers::pi_v<float> * 3 * k * t) / float(k);
                               SkPoint pt{r.left() + t * r.width(), r.centerY() - y * r.height() * 0.22f};
                               i ? path.lineTo(pt) : path.moveTo(pt);
                           }
                           p.setStrokeWidth(2.5f);
                           p.setColor(kPalette[3]);
                           c->drawPath(path.detach(), p);
                           c->restore();
                       }).height(130);
        add<HBox>().gap(10).alignItems(Align::Center).build([&](HBox& row) {
            Label& info = row.add<Label>("harmonics: 1").fontSize(12).color(kMuted).width(90);
            row.add<Button>("Add harmonic").onClick([this, w = &wave, l = &info] {
                m_harmonics = m_harmonics % 8 + 1;
                l->text("harmonics: " + std::to_string(m_harmonics));
                w->markPaintDirty(); // repaints the canvas only
            });
        });

        // ---- 2. Subclass: custom drawing + input ----
        header(*this, "2. Subclassing Widget",
               "Knob overrides onDraw + onMouseDown/Move/Up. Drag a knob up/down (hold Shift for fine control). "
               "Use a subclass when the widget has state or handles input; use Canvas when it only draws.");
        add<HBox>().gap(28).build([](HBox& knobs) {
            const struct { const char* name; SkColor color; float value; } defs[] = {
                {"Gain", kPalette[1], 0.7f}, {"Mix", kPalette[2], 0.35f}, {"Drive", kPalette[0], 0.9f}};
            for (auto& d : defs)
                knobs.add<VBox>().gap(4).alignItems(Align::Center).build([&](VBox& col) {
                    auto percent = [](float v) { char b[16]; std::snprintf(b, sizeof b, "%d%%", int(v * 100 + 0.5f)); return std::string(b); };
                    Knob& knob = col.add<Knob>(d.color, d.value);
                    Label& value = col.add<Label>(percent(d.value)).fontSize(12);
                    col.add<Label>(d.name).fontSize(11).color(kMuted);
                    knob.onChange([l = &value, percent](float v) { l->text(percent(v)); });
                });
        });

        // ---- 3. Manual layout: node graph ----
        header(*this, "3. manualLayout() - a node graph",
               "NodeGraph never uses Yoga for its children: it positions each node with setFrame(). Each node is still a VBox that Yoga "
               "lays out INSIDE its frame. Drag a node by its title: only that node and its wires repaint. Wires are drawn in "
               "NodeGraph::onDraw, which runs before the children, so the nodes sit on top.");
        add<NodeGraph>().height(300).build([](NodeGraph& g) {
            int noise = g.addNode("Noise", kPalette[3], {24, 24},
                                  {{"Scale", "4.0"}, {"Seed", "12"}, {"Value", "", false, true}});
            int grad = g.addNode("Gradient", kPalette[1], {250, 20}, {{"Input", "", true, false}, {"Color", "", false, true}});
            int blur = g.addNode("Blur", kPalette[5], {250, 140},
                                 {{"Image", "", true, false}, {"Radius", "8px"}, {"Result", "", false, true}});
            int out = g.addNode("Output", kPalette[2], {500, 80}, {{"Color", "", true, false}, {"Alpha", "", true, false}});
            g.connect(noise, 2, grad, 0);
            g.connect(noise, 2, blur, 0);
            g.connect(grad, 1, out, 0);
            g.connect(blur, 2, out, 1);
        });

        // ---- 4. absolute(): one child out of the flow ----
        header(*this, "4. absolute()",
               "Normal siblings flow; an absolute() child is taken out of the flow and placed at (x, y) inside its parent. "
               "Here a badge Canvas (with a Label child, centred by Yoga) overlays the corner of a card.");
        add<Panel>(SkColorSetRGB(44, 44, 48)).size(190, 80).alignItems(Align::Center).justify(Justify::Center)
            .build([](Panel& card) {
                card.add<Label>("Inbox").fontSize(14).weight(600);
                card.add<Canvas>([](SkCanvas* c, const SkRect& r) {
                        SkPaint p;
                        p.setAntiAlias(true);
                        p.setColor(kPalette[0]);
                        c->drawCircle(r.centerX(), r.centerY(), r.width() / 2, p);
                    })
                    .size(22, 22)
                    .absolute(190 - 22 - 6, 6) // top-right corner of the 190-wide card
                    .alignItems(Align::Center)
                    .justify(Justify::Center)
                    .build([](Canvas& badge) { badge.add<Label>("3").fontSize(11).weight(700).color(SK_ColorWHITE); });
            });
    }

private:
    int m_harmonics = 1; // read by the Canvas draw lambda
};

// ---------------------------------------------------------------- the other panels

class InspectorPanel : public Panel {
public:
    InspectorPanel() : Panel(SkColorSetRGB(28, 28, 28)) {
        padding(10).gap(6);
        add<Label>("Properties").fontSize(16).weight(600);
        add<Label>("Name").fontSize(12).color(kMuted);
        add<InputText>("Enter value...").onChange([](const std::string& v) { std::cout << "Input changed: " << v << "\n"; });
        add<Button>("Submit").onClick([] { std::cout << "Button clicked!\n"; });
    }
};

class OutlinerPanel : public Panel {
public:
    OutlinerPanel() : Panel(SkColorSetRGB(28, 28, 28)) {
        padding(10).gap(3);
        add<Label>("Scene").fontSize(16).weight(600);
        for (const char* n : {"Camera", "Key light", "Fill light", "Ground plane", "Character", "  Armature", "  Mesh"})
            add<Label>(n).fontSize(13);
    }
};

class ConsolePanel : public Panel {
public:
    ConsolePanel() : Panel(SkColorSetRGB(18, 18, 18)) {
        padding(8).gap(2);
        add<Label>("[info] Terracotta2 started").fontSize(12);
        add<Label>("[info] Drag a tab onto another pane's centre or edge to re-dock it").fontSize(12);
        add<Label>("[info] Drag a tab outside the window to float it").fontSize(12);
        add<Label>("[warn] This is demo output, nothing is being logged for real").fontSize(12).color(SkColorSetRGB(230, 180, 70));
    }
};

class ViewportPanel : public Panel {
public:
    ViewportPanel() : Panel(SkColorSetRGB(45, 45, 48)) {
        alignItems(Align::Center).justify(Justify::Center); // centred on both axes
        add<Label>("Viewport").fontSize(21).weight(600).color(kMuted);
        add<Label>("Open the Typography, Layout and Custom tabs next to this one").fontSize(12).color(kMuted);
    }
};

// Contents of the floating "Swatches" window: a wrapping row of chips.
class SwatchPanel : public Panel {
public:
    SwatchPanel() : Panel(SkColorSetRGB(28, 28, 28)) {
        direction(Direction::Row).wrap().gap(8).padding(12);
        for (int i = 0; i < 12; ++i) box(*this, kPalette[i % 6], 48, 48);
    }
};

} // namespace

int main() {
    try {
        Application app;
        // App-wide default font: set it before building any UI. Any installed family works, or
        // register your own file:  theme().fontFamily = loadFont("/path/to/MyFont.ttf");
        theme().fontFamily = "Helvetica Neue";

        Window* window = app.createWindow({.title = "Terracotta2 - Layout & Text", .width = 1600, .height = 900});
        DockSpace& dockSpace = window->root()->add<DockSpace>();

        // left column | right column;  left column: tabbed top over console
        auto [leftPane, rightPane] = dockSpace.splitHorizontal(0.70f);
        auto [topLeftPane, bottomLeftPane] = leftPane->splitVertical(0.78f);

        topLeftPane->setTabTitle("Viewport");
        topLeftPane->setContent<ViewportPanel>();
        topLeftPane->addTab<TypographyPage>("Typography");
        topLeftPane->addTab<LayoutPage>("Layout");
        topLeftPane->addTab<CustomPage>("Custom");
        topLeftPane->setActiveTab(1); // Typography

        bottomLeftPane->setTabTitle("Console");
        bottomLeftPane->setContent<ConsolePanel>();

        rightPane->setTabTitle("Inspector");
        rightPane->setContent<InspectorPanel>();
        rightPane->addTab<OutlinerPanel>("Outliner");
        rightPane->setActiveTab(0);

        // A second OS window, docked-style: drag its tab back into the main window to merge it.
        SkRect main = window->screenFrame();
        dockSpace.floatTab<SwatchPanel>("Swatches", SkRect::MakeXYWH(main.right() - 320, main.top() + 120, 260, 190));

        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return -1;
    }
    return 0;
}
