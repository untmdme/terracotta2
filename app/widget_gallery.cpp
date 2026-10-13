// Terracotta2 widget gallery: every built-in widget, plus two custom ones built in this file (Spinner, ContextArea).
#include <terracotta2/Terracotta2.hpp>
#include "include/core/SkPaint.h"
#include <algorithm>
#include <iostream>

using namespace Terracotta2;

namespace {

// ---------------------------------------------------------------- custom widgets

// Loading spinner: a 1 s linear tween restarted from onComplete loops forever. Each tick
// marks only the spinner dirty, so only its 32x32 square repaints (see TC2_SHOW_DAMAGE=1).
class Spinner : public Widget {
public:
    Spinner() { size(32, 32); spin(); } // animate() before attaching is fine: it starts once added
    void setSpinning(bool on) {
        m_on = on;
        if (on) spin(); else m_anim.stop();
        markPaintDirty();
    }
    void onDraw(SkCanvas* c) override {
        SkRect r = m_bounds.makeInset(3, 3);
        SkPaint p;
        p.setAntiAlias(true);
        p.setStyle(SkPaint::kStroke_Style);
        p.setStrokeWidth(3);
        p.setStrokeCap(SkPaint::kRound_Cap);
        p.setColor(theme().track);
        c->drawOval(r, p);
        p.setColor(m_on ? theme().accent : theme().textMuted);
        c->drawArc(r, m_angle, 100, false, p);
    }

private:
    void spin() {
        animate(1.0f, [this](float t) { m_angle = t * 360 - 90; markPaintDirty(); }, [this] { if (m_on) spin(); },
                Ease::Linear);
    }
    bool m_on = true;
    float m_angle = -90;
};

// Right-click anywhere on it for a context menu: openMenu at the pointer, in window coordinates.
class ContextArea : public Panel {
public:
    ContextArea(Label& status) : Panel(theme().input.background), m_status(status) {
        height(70).justify(Justify::Center).alignItems(Align::Center);
        add<Label>("Right-click here").color(theme().textMuted);
    }
    bool onMouseDown(const MouseEvent& e) override { return e.button == MouseButton::Right; }
    bool onMouseClick(const MouseEvent& e) override {
        if (e.button != MouseButton::Right) return false;
        static const std::vector<std::string> items = {"Cut", "Copy", "Paste", "Delete"};
        openMenu(*this, mapToWindow(SkPoint{e.x, e.y}), items, [this](int i) { m_status.text("Context menu: " + items[i]); });
        return true;
    }

private:
    Label& m_status;
};

// ---------------------------------------------------------------- helpers

// A card with a title and a muted caption; add the section's widgets to the returned Panel.
Panel& section(Widget& page, const char* title, const char* caption) {
    Panel& card = page.add<Panel>().padding(16).gap(10);
    card.add<Label>(title).fontSize(16).weight(600);
    card.add<Label>(caption).fontSize(12).color(theme().textMuted);
    return card;
}

Panel& swatch(Widget& parent, SkColor color, float w, float h) { return parent.add<Panel>(color).size(w, h); }

// A captioned dark box, for the container examples.
Panel& tile(Widget& row, const char* caption) {
    VBox& t = row.add<VBox>().gap(4);
    t.add<Label>(caption).fontSize(11).color(theme().textMuted);
    return t.add<Panel>(theme().input.background).padding(10).gap(6);
}

} // namespace

int main() {
    try {
        Application app;
        Window* window = app.createWindow({.title = "Terracotta2 - Widget Gallery", .width = 1100, .height = 760, .minWidth = 560, .minHeight = 400});
        ScrollView& page = window->root()->add<ScrollView>().padding(20).padding(Edge::Right, 26).gap(16);

        // Designer: theme editor + widget inspector in its own window; edits apply live.
        page.add<Button>("Open designer").onClick([] { openDesigner(); });

        // Animation: the spinner repaints every frame, nothing else does.
        section(page, "Animation", "Spinner loops animate(..., Ease::Linear) from onComplete. Run with TC2_SHOW_DAMAGE=1: only its square is outlined each frame; the rest of the window is never repainted. Stop it and the app goes back to 0 frames.")
            .build([&](Panel& card) {
                card.add<HBox>().gap(16).alignItems(Align::Center).build([&](HBox& row) {
                    auto& spinner = row.add<Spinner>();
                    row.add<Toggle>(true).onChange([s = &spinner](bool on) { s->setSpinning(on); });
                    row.add<Label>("Spinning").color(theme().textMuted);
                });
            });

        // Text: Label size, weight, colour, font; long text wraps to the width its parent gives it.
        section(page, "Text", "Label(text).fontSize().weight().color().font().lineHeight()").build([](Panel& card) {
            card.add<HBox>().gap(16).alignItems(Align::Center).build([](HBox& row) {
                for (float size : {11, 13, 16, 20, 28}) row.add<Label>("Size " + std::to_string(int(size))).fontSize(size);
                for (int weight : {200, 400, 600, 800}) row.add<Label>("Weight " + std::to_string(weight)).fontSize(16).weight(weight);
            });
            card.add<HBox>().gap(16).alignItems(Align::Center).build([](HBox& row) {
                row.add<Label>("Accent").fontSize(16).color(SkColorSetRGB(90, 150, 240));
                row.add<Label>("Warning").fontSize(16).color(SkColorSetRGB(230, 170, 60));
                row.add<Label>("Success").fontSize(16).color(SkColorSetRGB(110, 180, 90));
                for (const char* family : {"Georgia", "Menlo", "Helvetica Neue"}) row.add<Label>(family).fontSize(16).font(family);
            });
            card.add<Label>("Long text wraps on its own. Put a Label in a column and it becomes a paragraph; cap its width "
                            "with maxSize() for a comfortable line length and set lineHeight() for air between lines. Every "
                            "widget is a flexbox item, so there is no separate text-layout API to learn.")
                .lineHeight(1.5f).maxWidth(640);
        });

        // Handles for widgets that callbacks need before they are declared: ref(ptr) fills them in mid-chain.
        Label *clicks = nullptr, *percent = nullptr;
        int count = 0;
        Slider* slider = nullptr;
        ProgressBar* bar = nullptr;
        Canvas* chart = nullptr;

        // Buttons: Button(text).onClick(fn); the callback updates a Label through .text().
        section(page, "Buttons", "Button(text).onClick(fn). Tab moves focus, Space or Enter clicks. Hover and press states come from theme().button.").build([&](Panel& card) {
            card.add<HBox>().gap(8).alignItems(Align::Center).build([&](HBox& row) {
                row.add<Button>("Click me").onClick([&] { clicks->text("Clicked " + std::to_string(++count) + " times"); });
                row.add<Button>("Reset").onClick([&] { clicks->text("No clicks yet"); count = 0; }).tooltip("Sets the counter back to zero");
                row.add<Button>("Print to stdout").onClick([] { std::cout << "Hello from a button\n"; });
            });
            card.add<Label>("No clicks yet").color(theme().textMuted).ref(clicks);
        });

        // Text input: onChange fires per edit; here it mirrors into a Label.
        section(page, "Text input", "InputText(placeholder).onChange(fn). Type, then drag or Shift+arrows to select; Cmd+A, C, X and V work as usual.")
            .build([](Panel& card) {
                auto* hello = &card.add<Label>("Hello, stranger!").fontSize(20);
                card.add<InputText>("Your name").onChange([hello](const std::string& name) { hello->text("Hello, " + (name.empty() ? "stranger" : name) + "!"); })
                    .width(280);
                card.add<Label>("TextArea(placeholder).rows(3).maxRows(8): Enter adds a line, the field grows to 8 lines, then scrolls.").fontSize(12).color(theme().textMuted);
                card.add<TextArea>("Notes...").rows(3).maxRows(8).width(420);
            });

        // Selectable text: opt in per Label.
        section(page, "Selectable text", "Label(text).selectable(true)").build([](Panel& card) {
            card.add<Label>("Any Label can be made selectable. Double-click picks a word, Cmd+A takes everything, and the text "
                            "stays an ordinary flexbox item, so it wraps and lays out like the rest of the page.")
                .selectable(true).lineHeight(1.5f).maxWidth(640);
            card.add<Label>("Drag to select, Cmd+C to copy").fontSize(12).color(theme().textMuted);
        });

        // Containers: VBox / HBox / Panel, with padding, gap, grow and nesting.
        section(page, "Containers", "VBox stacks, HBox lines up, Panel adds a background. padding is inside, gap is between, grow shares leftover space.")
            .add<HBox>().gap(16).alignItems(Align::Start).build([](HBox& row) {
                tile(row, "VBox: padding(10).gap(6)").build([](Panel& p) {
                    for (SkColor c : {SkColorSetRGB(214, 90, 70), SkColorSetRGB(230, 160, 60), SkColorSetRGB(110, 180, 90)})
                        swatch(p, c, 120, 24);
                });
                tile(row, "HBox: grow(1) and grow(2)").width(240).direction(Direction::Row).build([](Panel& p) {
                    swatch(p, SkColorSetRGB(60, 160, 190), 0, 40).grow(1);
                    swatch(p, SkColorSetRGB(100, 120, 210), 0, 40).grow(2);
                });
                tile(row, "Nested card").build([](Panel& p) {
                    p.add<Panel>(theme().panelBackground).padding(10).gap(6).build([](Panel& card) {
                        card.add<Label>("Card title").weight(600);
                        card.add<Label>("Panels nest freely.").fontSize(12).color(theme().textMuted);
                        card.add<Button>("Action").onClick([] {});
                    });
                });
            });

        // Value controls: value() reads, value(v) sets, onChange(fn) fires on user edits.
        // Their callbacks reach widgets declared further down (percent, bar, chart) through the ref() handles.
        auto syncSlider = [&](float v) {
            percent->text(std::to_string(int(v * 100 + 0.5f)) + "%");
            bar->value(v);
            chart->markPaintDirty();
        };
        section(page, "Value controls", "Toggle, Checkbox, Slider and ProgressBar. Drag the slider (or focus it with Tab and use the arrows): the bar, the label and the chart below follow.")
            .build([&](Panel& card) {
                card.add<HBox>().gap(24).alignItems(Align::Center).build([&](HBox& row) {
                    row.add<Toggle>(true).onChange([&](bool on) { chart->visible(on); });
                    row.add<Label>("Show chart");
                    row.add<Checkbox>("Show percentage", true).onChange([&](bool on) { percent->visible(on); });
                });
                card.add<HBox>().gap(12).alignItems(Align::Center).build([&](HBox& row) {
                    row.add<Slider>().width(320).ref(slider).onChange(syncSlider);
                    row.add<Label>("").width(44).ref(percent);
                });
                card.add<ProgressBar>().width(320).ref(bar);
            });

        // Dropdowns and menus: arrows move, Enter picks, Escape closes; focus returns to the opener.
        section(page, "Dropdowns and menus", "Dropdown(items, selected).onChange(fn) and openMenu(widget, anchor, items, onPick). With the keyboard: Tab to the dropdown, Space to open, arrows, Enter.")
            .build([](Panel& card) {
                Label& status = card.add<Label>("Nothing picked yet").color(theme().textMuted);
                card.add<HBox>().gap(12).alignItems(Align::Center).build([&](HBox& row) {
                    row.add<Dropdown>(std::vector<std::string>{"Small", "Medium", "Large"}, 1).width(140)
                        .onChange([&status](int i) { status.text("Dropdown: index " + std::to_string(i)); });
                    Button& menuButton = row.add<Button>("Open menu");
                    menuButton.onClick([&status, b = &menuButton] {
                        const SkRect r = b->mapToWindow(b->bounds());
                        openMenu(*b, r.makeOutset(0, 2), {"New", "Open...", "Save", "Save As..."},
                                 [&status](int i) { status.text("Menu: item " + std::to_string(i)); });
                    });
                });
                card.add<ContextArea>(status).width(320);
            });

        // Tooltips: any widget, .tooltip(text). Delay and colours come from theme().tooltip.
        section(page, "Tooltips", "widget.tooltip(text). Rest the pointer on a button; a click hides it.").build([](Panel& card) {
            card.add<HBox>().gap(8).build([](HBox& row) {
                row.add<Button>("Save").tooltip("Write the document to disk (Cmd+S)");
                row.add<Button>("Export").tooltip("Save a copy as PNG, SVG or PDF");
                row.add<Toggle>(false).tooltip("Toggles take tooltips too, like every widget");
            });
        });

        // Custom title bar: a window that draws its own bar (see also custom_titlebar.cpp).
        section(page, "Custom title bar", "WindowDesc{.customTitlebar = true} + TitleBar + WindowButtons. The bar drags the window; buttons in it stay clickable.")
            .add<Button>("Open a window with a custom title bar").onClick([] {
                Window* w = Application::get().createWindow({.title = "Custom title bar", .width = 420, .height = 220, .minWidth = 260, .minHeight = 120, .customTitlebar = true});
                w->root()->add<TitleBar>().build([](TitleBar& bar) {
                    bar.add<Label>("My tool window").weight(600).grow(1);
                    bar.add<WindowButtons>();
                });
                w->root()->add<Panel>().grow(1).justify(Justify::Center).alignItems(Align::Center).build([](Panel& p) {
                    p.add<Label>("Drag the bar above to move this window.").color(theme().textMuted);
                });
            }).alignSelf(Align::Start);

        // Editors bound to data that lives elsewhere: they read and write through get/set.
        static float amount = 42;
        static SkColor swatchColor = SkColorSetRGB(70, 130, 230);
        section(page, "Value editors", "DragNumber(get, set, min, max) and ColorPicker(get, set) edit values that live elsewhere; refresh() re-reads them. Drag the number sideways.")
            .build([](Panel& card) {
                card.add<HBox>().gap(24).alignItems(Align::Start).build([](HBox& row) {
                    row.add<DragNumber>([] { return amount; }, [](float v) { amount = v; }, 0, 100).width(140);
                    Panel& preview = row.add<Panel>(swatchColor).size(60, 60);
                    row.add<ColorPicker>([] { return swatchColor; }, [p = &preview](SkColor c) { swatchColor = c; p->background(c); });
                });
            });

        // Canvas: a draw lambda, no subclass needed. markPaintDirty() asks for a repaint.
        section(page, "Canvas", "Canvas(lambda) for custom drawing. This donut reads slider->value() on every repaint.").build([&](Panel& card) {
            card.add<HBox>().gap(16).alignItems(Align::Center).build([&](HBox& row) {
                row.add<Canvas>([&](SkCanvas* c, const SkRect& r) {
                        SkRect ring = r.makeInset(12, 12);
                        SkPaint p;
                        p.setAntiAlias(true);
                        p.setStyle(SkPaint::kStroke_Style);
                        p.setStrokeWidth(18);
                        p.setColor(theme().track);
                        c->drawOval(ring, p);
                        p.setColor(theme().accent);
                        c->drawArc(ring, -90, 360 * slider->value(), false, p);
                    })
                    .size(140, 140)
                    .ref(chart);
                row.add<Label>("Same value, three views: ProgressBar, Label and Canvas.").color(theme().textMuted);
            });
        });

        slider->value(0.35f); // everything exists now: sync the views once
        syncSlider(0.35f);

        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return -1;
    }
    return 0;
}
