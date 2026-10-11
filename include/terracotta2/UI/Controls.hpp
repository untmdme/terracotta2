#pragma once
#include <functional>
#include <memory>
#include <string>
#include "include/core/SkColor.h"
#include "terracotta2/UI/Widget.hpp"

namespace skia::textlayout { class Paragraph; }

namespace Terracotta2 {

// Registers a font file (.ttf/.otf/.ttc) for all text. Returns its family name, to use with
// Label::font() or theme().fontFamily; empty string if the file can't be loaded.
std::string loadFont(const std::string& path);

// Single-style text. Measures itself through a Yoga measure function (skparagraph);
// the shaped paragraph is cached and only rebuilt when text/style/width change.
// Wraps to the width its container gives it, so long text in a column becomes a paragraph.
// In a row, give it room to wrap with setFlexShrink(1) (or flexGrow + flexBasis 0).
// Labels don't shrink by default: Yoga has no CSS min-height:auto, so a shrinkable label
// in an overflowing column would be squashed below its text height.
class Label : public Widget {
public:
    explicit Label(std::string text, float fontSize = 0.0f /* 0 = theme().label.fontSize */, int weight = 400 /* 100..900 */);
    ~Label() override;

    // Chainable. text/size/weight/lineHeight/font re-measure (layout); color repaints only.
    Label& text(std::string text);
    const std::string& text() const { return m_text; }
    Label& color(SkColor color);
    Label& fontSize(float size);          // 0 = theme().label.fontSize (not size(): that is Widget::size(w, h))
    Label& weight(int weight);            // 100..900 (400 regular, 700 bold)
    Label& lineHeight(float multiplier);  // e.g. 1.4 for body text; 0 = font default
    Label& font(std::string family);      // "" = theme().fontFamily; system family or one from loadFont()
    float fontSize() const { return m_fontSize; }
    int weight() const { return m_weight; }
    SkColor color() const { return m_color; }
    float lineHeight() const { return m_lineHeight; }
    const std::string& font() const { return m_family; }
    // Opt-in: drag to select, double-click selects a word, Cmd/Ctrl+A selects all, Cmd/Ctrl+C copies.
    Label& selectable(bool on = true);
    bool isSelectable() const { return m_selectable; }
    std::string selectedText() const;

    bool isFocusable() const override { return m_selectable; }
    Cursor cursor() const override { return m_selectable ? Cursor::IBeam : Cursor::Arrow; }
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onKey(const KeyEvent& e) override;
    void onBlur() override; // clears the selection
    void onThemeChanged() override;

    void onDraw(SkCanvas* canvas) override;

    // Internal: used by the Yoga measure callback.
    SkSize measure(float maxWidth);

private:
    std::string m_text;
    float m_fontSize;
    int m_weight;
    float m_lineHeight = 0.0f;
    std::string m_family;
    SkColor m_color;
    bool m_themeSize, m_themeColor = true; // follow theme().label until fontSize()/color() is set
    bool m_selectable = false;
    bool m_selecting = false;          // mouse drag in progress
    size_t m_selAnchor = 0, m_selEnd = 0; // UTF-8 byte offsets; empty when equal
    std::unique_ptr<skia::textlayout::Paragraph> m_paragraph;
    float m_layoutWidth = -1.0f;
};

class Button : public Widget {
public:
    Button(std::string text, std::function<void()> onClick);

    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onClick(const MouseEvent& e) override;
    void onMouseEnter() override;
    void onMouseLeave() override;
    Cursor cursor() const override { return Cursor::Hand; }
    void onThemeChanged() override;
    void onDraw(SkCanvas* canvas) override;

private:
    std::function<void()> m_onClick;
    Label* m_label;
    float m_hover = 0.0f; // 0..1, animated
    bool m_pressed = false;
};

// Single-line text field. onChange fires on every edit.
class InputText : public Widget {
public:
    InputText(std::string placeholder, std::function<void(const std::string&)> onChange);
    ~InputText() override;

    const std::string& value() const { return m_value; }
    void setValue(std::string v);

    bool isFocusable() const override { return true; }
    Cursor cursor() const override { return Cursor::IBeam; }
    // Selection: drag, Shift+arrows/Home/End, double-click word, Cmd/Ctrl+A/C/X/V; typing replaces it.
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onKey(const KeyEvent& e) override;
    bool onChar(uint32_t codepoint) override;
    void onFocus() override;
    void onBlur() override;
    void onThemeChanged() override;
    void onDraw(SkCanvas* canvas) override;

private:
    void edited();
    std::string m_placeholder;
    std::string m_value;
    size_t m_caret = 0;  // byte offset into m_value, always on a UTF-8 boundary
    size_t m_anchor = 0; // selection = [min(anchor, caret), max(...)); empty when equal
    bool m_selecting = false;
    bool m_focused = false;
    std::function<void(const std::string&)> m_onChange;
    std::unique_ptr<skia::textlayout::Paragraph> m_paragraph; // cached for current value/placeholder
};

// Number edited by dragging horizontally, like imgui's DragFloat; the filled part shows where the
// value sits in [min, max]. Reads and writes through get/set, so it can edit any value.
// NaN from get() shows "auto" (unset Yoga sizes); a drag then starts from 0.
class DragNumber : public Widget {
public:
    DragNumber(std::function<float()> get, std::function<void(float)> set, float min, float max,
               float perPixel = 1.0f, int decimals = 0);
    void refresh(); // re-read get(), e.g. after the value changed elsewhere
    // Where a drag starts while get() is NaN ("auto"), e.g. the widget's current size. Default 0.
    DragNumber& whenAuto(std::function<float()> fn) { m_auto = std::move(fn); return *this; }

    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    Cursor cursor() const override { return Cursor::HResize; }
    void onThemeChanged() override { refresh(); }
    void onDraw(SkCanvas* canvas) override;

private:
    std::function<float()> m_get;
    std::function<void(float)> m_set;
    std::function<float()> m_auto;
    float m_min, m_max, m_perPixel;
    int m_decimals;
    Label* m_label;
    float m_grabX = 0, m_grabValue = 0;
    bool m_drag = false;
};

// HSV color picker: saturation/value square, hue bar, alpha bar and a hex field
// (#RGB, #RRGGBB or #RRGGBBAA). Reads and writes through get/set, like DragNumber.
class ColorPicker : public Widget {
public:
    ColorPicker(std::function<SkColor()> get, std::function<void(SkColor)> set);
    void refresh(); // re-read get(), e.g. after the color changed elsewhere

    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    void onThemeChanged() override { refresh(); }
    void onDraw(SkCanvas* canvas) override;

private:
    enum class Part { None, SV, Hue, Alpha };
    Part partAt(float x, float y) const;
    void dragTo(float x, float y);
    void apply(); // m_hsv + m_alpha -> set()
    SkRect svRect() const, hueRect() const, alphaRect() const;
    std::function<SkColor()> m_get;
    std::function<void(SkColor)> m_set;
    float m_hsv[3] = {0, 0, 0}; // kept apart from the color so hue survives grey/black
    uint8_t m_alpha = 255;
    SkColor m_shown = 0;        // last color reflected in m_hsv / the hex field
    Part m_drag = Part::None;
    InputText* m_hex;
};

// "#RRGGBB" (or "#RRGGBBAA" when not opaque), and the reverse; false if `hex` is malformed.
std::string colorToHex(SkColor c);
bool hexToColor(const std::string& hex, SkColor& out);

} // namespace Terracotta2
