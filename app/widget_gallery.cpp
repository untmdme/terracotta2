// Terracotta2 widget gallery: what's in the box, plus four custom widgets built in this file.
#include <terracotta2/Core/Application.hpp>
#include <terracotta2/Core/Window.hpp>
#include <terracotta2/Tools/Designer.hpp>
#include <terracotta2/UI/Container.hpp>
#include <terracotta2/UI/Controls.hpp>
#include "include/core/SkPaint.h"
#include <algorithm>
#include <iostream>

using namespace Terracotta2;

namespace {

// ---------------------------------------------------------------- custom widgets

// On/off switch; onClick() fires on press+release over us, and the knob slides via animate().
class Toggle : public Widget {
public:
    explicit Toggle(bool on = false) : m_on(on), m_t(on) { size(44, 24); }
    Toggle& onChange(std::function<void(bool)> fn) { m_onChange = std::move(fn); return *this; }

    bool onClick(const MouseEvent&) override {
        m_on = !m_on;
        float from = m_t, to = m_on ? 1.0f : 0.0f;
        animate(0.15f, [=, this](float k) { m_t = from + (to - from) * k; markPaintDirty(); });
        if (m_onChange) m_onChange(m_on);
        return true;
    }
    Cursor cursor() const override { return Cursor::Hand; }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(theme().track);
        c->drawRoundRect(m_bounds, 12, 12, p);
        p.setColor(SkColorSetA(theme().accent, uint8_t(255 * m_t)));
        c->drawRoundRect(m_bounds, 12, 12, p);
        p.setColor(SK_ColorWHITE);
        c->drawCircle(m_bounds.left() + 12 + 20 * m_t, m_bounds.centerY(), 9, p);
    }

private:
    bool m_on;
    float m_t; // knob position 0..1
    std::function<void(bool)> m_onChange;
};

// Horizontal slider, value 0..1. Returning true from onMouseDown captures the drag.
class Slider : public Widget {
public:
    Slider() { height(20); }
    Slider& onChange(std::function<void(float)> fn) { m_onChange = std::move(fn); return *this; }
    float value() const { return m_value; }
    void setValue(float v) {
        m_value = std::clamp(v, 0.0f, 1.0f);
        markPaintDirty();
        if (m_onChange) m_onChange(m_value);
    }

    bool onMouseDown(const MouseEvent& e) override { m_drag = true; dragTo(e.x); return true; }
    bool onMouseMove(const MouseEvent& e) override { if (m_drag) dragTo(e.x); return m_drag; }
    bool onMouseUp(const MouseEvent&) override { m_drag = false; return true; }
    Cursor cursor() const override { return Cursor::Hand; }
    void onDraw(SkCanvas* c) override {
        float x0 = m_bounds.left() + kKnob, x1 = m_bounds.right() - kKnob, y = m_bounds.centerY();
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(theme().track);
        c->drawRoundRect(SkRect::MakeLTRB(x0, y - 2, x1, y + 2), 2, 2, p);
        p.setColor(theme().accent);
        c->drawRoundRect(SkRect::MakeLTRB(x0, y - 2, x0 + (x1 - x0) * m_value, y + 2), 2, 2, p);
        p.setColor(SK_ColorWHITE);
        c->drawCircle(x0 + (x1 - x0) * m_value, y, kKnob, p);
    }

private:
    static constexpr float kKnob = 8;
    void dragTo(float x) { setValue((x - m_bounds.left() - kKnob) / (m_bounds.width() - 2 * kKnob)); }
    float m_value = 0;
    bool m_drag = false;
    std::function<void(float)> m_onChange;
};

// Thin read-only bar, value 0..1.
class ProgressBar : public Widget {
public:
    ProgressBar() { height(8); }
    void set(float v) { m_value = v; markPaintDirty(); }
    void onDraw(SkCanvas* c) override {
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(theme().track);
        c->drawRoundRect(m_bounds, 4, 4, p);
        p.setColor(theme().accent);
        c->drawRoundRect(SkRect::MakeXYWH(m_bounds.left(), m_bounds.top(), m_bounds.width() * m_value, m_bounds.height()), 4, 4, p);
    }

private:
    float m_value = 0;
};

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

// Box + text. The Label is an ordinary child, so layout comes for free; clicks on it bubble up to our onClick.
class Checkbox : public Widget {
public:
    explicit Checkbox(std::string text, bool checked = false) : m_checked(checked) {
        padding(YGEdgeLeft, 26).minSize(0, 20).justify(YGJustifyCenter);
        add<Label>(std::move(text));
    }
    Checkbox& onChange(std::function<void(bool)> fn) { m_onChange = std::move(fn); return *this; }

    bool onClick(const MouseEvent&) override {
        m_checked = !m_checked;
        markPaintDirty();
        if (m_onChange) m_onChange(m_checked);
        return true;
    }
    Cursor cursor() const override { return Cursor::Hand; }
    void onMouseEnter() override { markPaintDirty(); } // isHovered() changed: repaint
    void onMouseLeave() override { markPaintDirty(); }
    void onDraw(SkCanvas* c) override {
        SkRect box = SkRect::MakeXYWH(m_bounds.left(), m_bounds.centerY() - 9, 18, 18);
        SkPaint p;
        p.setAntiAlias(true);
        p.setColor(m_checked ? theme().accent : theme().input.background);
        c->drawRoundRect(box, 4, 4, p);
        p.setStyle(SkPaint::kStroke_Style);
        p.setColor(m_checked || isHovered() ? theme().accent : theme().input.border);
        c->drawRoundRect(box, 4, 4, p);
        if (!m_checked) return;
        p.setColor(SK_ColorWHITE);
        p.setStrokeWidth(2);
        p.setStrokeCap(SkPaint::kRound_Cap);
        float x = box.left(), y = box.top();
        c->drawLine(x + 4.5f, y + 9.5f, x + 7.5f, y + 12.5f, p);
        c->drawLine(x + 7.5f, y + 12.5f, x + 13.5f, y + 5.5f, p);
    }

private:
    bool m_checked;
    std::function<void(bool)> m_onChange;
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
        Window* window = app.createWindow({.title = "Terracotta2 - Widget Gallery", .width = 1100, .height = 760});
        ScrollView& page = window->getRootWidget()->add<ScrollView>().padding(20).padding(YGEdgeRight, 26).gap(16);

        // Designer: theme editor + widget inspector in its own window; edits apply live.
        page.add<Button>("Open designer", [] { openDesigner(); });

        // Animation: the spinner repaints every frame, nothing else does.
        section(page, "Animation", "Spinner loops animate(..., Ease::Linear) from onComplete. Run with TC2_SHOW_DAMAGE=1: only its square is outlined each frame; the rest of the window is never repainted. Stop it and the app goes back to 0 frames.")
            .build([&](Panel& card) {
                card.add<HBox>().gap(16).alignItems(YGAlignCenter).build([&](HBox& row) {
                    auto& spinner = row.add<Spinner>();
                    row.add<Toggle>(true).onChange([s = &spinner](bool on) { s->setSpinning(on); });
                    row.add<Label>("Spinning").color(theme().textMuted);
                });
            });

        // Text: Label size, weight, colour, font; long text wraps to the width its parent gives it.
        section(page, "Text", "Label(text).fontSize().weight().color().font().lineHeight()").build([](Panel& card) {
            card.add<HBox>().gap(16).alignItems(YGAlignCenter).build([](HBox& row) {
                for (float size : {11, 13, 16, 20, 28}) row.add<Label>("Size " + std::to_string(int(size))).fontSize(size);
                for (int weight : {200, 400, 600, 800}) row.add<Label>("Weight " + std::to_string(weight)).fontSize(16).weight(weight);
            });
            card.add<HBox>().gap(16).alignItems(YGAlignCenter).build([](HBox& row) {
                row.add<Label>("Accent").fontSize(16).color(SkColorSetRGB(90, 150, 240));
                row.add<Label>("Warning").fontSize(16).color(SkColorSetRGB(230, 170, 60));
                row.add<Label>("Success").fontSize(16).color(SkColorSetRGB(110, 180, 90));
                for (const char* family : {"Georgia", "Menlo", "Helvetica Neue"}) row.add<Label>(family).fontSize(16).font(family);
            });
            card.add<Label>("Long text wraps on its own. Put a Label in a column and it becomes a paragraph; cap its width "
                            "with maxSize() for a comfortable line length and set lineHeight() for air between lines. Every "
                            "widget is a flexbox item, so there is no separate text-layout API to learn.")
                .lineHeight(1.5f).maxSize(640, YGUndefined);
        });

        // Handles for widgets that callbacks need before they are declared: ref(ptr) fills them in mid-chain.
        Label *clicks = nullptr, *percent = nullptr;
        int count = 0;
        Slider* slider = nullptr;
        ProgressBar* bar = nullptr;
        Canvas* chart = nullptr;

        // Buttons: Button(text, onClick); the callback updates a Label through .text().
        section(page, "Buttons", "Button(text, onClick). Hover and press states come from theme().button.").build([&](Panel& card) {
            card.add<HBox>().gap(8).alignItems(YGAlignCenter).build([&](HBox& row) {
                row.add<Button>("Click me", [&] { clicks->text("Clicked " + std::to_string(++count) + " times"); });
                row.add<Button>("Reset", [&] { clicks->text("No clicks yet"); count = 0; });
                row.add<Button>("Print to stdout", [] { std::cout << "Hello from a button\n"; });
            });
            card.add<Label>("No clicks yet").color(theme().textMuted).ref(clicks);
        });

        // Text input: onChange fires per edit; here it mirrors into a Label.
        section(page, "Text input", "InputText(placeholder, onChange). Type, then drag or Shift+arrows to select; Cmd+A, C, X and V work as usual.")
            .build([](Panel& card) {
                auto* hello = &card.add<Label>("Hello, stranger!").fontSize(20);
                card.add<InputText>("Your name", [hello](const std::string& name) { hello->text("Hello, " + (name.empty() ? "stranger" : name) + "!"); })
                    .width(280);
            });

        // Selectable text: opt in per Label.
        section(page, "Selectable text", "Label(text).selectable(true)").build([](Panel& card) {
            card.add<Label>("Any Label can be made selectable. Double-click picks a word, Cmd+A takes everything, and the text "
                            "stays an ordinary flexbox item, so it wraps and lays out like the rest of the page.")
                .selectable(true).lineHeight(1.5f).maxSize(640, YGUndefined);
            card.add<Label>("Drag to select, Cmd+C to copy").fontSize(12).color(theme().textMuted);
        });

        // Containers: VBox / HBox / Panel, with padding, gap, grow and nesting.
        section(page, "Containers", "VBox stacks, HBox lines up, Panel adds a background. padding is inside, gap is between, grow shares leftover space.")
            .add<HBox>().gap(16).alignItems(YGAlignFlexStart).build([](HBox& row) {
                tile(row, "VBox: padding(10).gap(6)").build([](Panel& p) {
                    for (SkColor c : {SkColorSetRGB(214, 90, 70), SkColorSetRGB(230, 160, 60), SkColorSetRGB(110, 180, 90)})
                        swatch(p, c, 120, 24);
                });
                tile(row, "HBox: grow(1) and grow(2)").width(240).direction(YGFlexDirectionRow).build([](Panel& p) {
                    swatch(p, SkColorSetRGB(60, 160, 190), 0, 40).grow(1);
                    swatch(p, SkColorSetRGB(100, 120, 210), 0, 40).grow(2);
                });
                tile(row, "Nested card").build([](Panel& p) {
                    p.add<Panel>(theme().panelBackground).padding(10).gap(6).build([](Panel& card) {
                        card.add<Label>("Card title").weight(600);
                        card.add<Label>("Panels nest freely.").fontSize(12).color(theme().textMuted);
                        card.add<Button>("Action", [] {});
                    });
                });
            });

        // Custom widgets: Toggle, Slider, ProgressBar and Checkbox, all defined at the top of this file.
        // Their callbacks reach widgets declared further down (percent, bar, chart) through the ref() handles.
        section(page, "Custom widgets", "Subclass Widget, override onDraw and onClick / the mouse handlers. Drag the slider: the bar, the label and the chart below follow.")
            .build([&](Panel& card) {
                card.add<HBox>().gap(24).alignItems(YGAlignCenter).build([&](HBox& row) {
                    row.add<Toggle>(true).onChange([&](bool on) { chart->visible(on); });
                    row.add<Label>("Show chart");
                    row.add<Checkbox>("Show percentage", true).onChange([&](bool on) { percent->visible(on); });
                });
                card.add<HBox>().gap(12).alignItems(YGAlignCenter).build([&](HBox& row) {
                    row.add<Slider>().width(320).ref(slider).onChange([&](float v) {
                        percent->text(std::to_string(int(v * 100 + 0.5f)) + "%");
                        bar->set(v);
                        chart->markPaintDirty();
                    });
                    row.add<Label>("").width(44).ref(percent);
                });
                card.add<ProgressBar>().width(320).ref(bar);
            });

        // Canvas: a draw lambda, no subclass needed. markPaintDirty() asks for a repaint.
        section(page, "Canvas", "Canvas(lambda) for custom drawing. This donut reads slider->value() on every repaint.").build([&](Panel& card) {
            card.add<HBox>().gap(16).alignItems(YGAlignCenter).build([&](HBox& row) {
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

        slider->setValue(0.35f); // everything exists now: this fires onChange once to sync the views

        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return -1;
    }
    return 0;
}
