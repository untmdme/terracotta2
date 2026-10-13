#pragma once
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
    bool isTabStop() const override { return false; } // selectable text is clicked into, not tabbed to
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

} // namespace Terracotta2
