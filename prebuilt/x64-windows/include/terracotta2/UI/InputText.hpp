#pragma once
#include <functional>
#include <memory>
#include <string>
#include "include/core/SkRect.h"
#include "terracotta2/UI/Widget.hpp"

namespace skia::textlayout { class Paragraph; }

namespace Terracotta2 {

// Single-line text field. onChange fires on every edit by the user (not on value(v)).
// Selection: drag, Shift+arrows/Home/End, double-click word, Cmd/Ctrl+A/C/X/V; typing replaces it.
class InputText : public Widget {
public:
    explicit InputText(std::string placeholder = {}) : InputText(std::move(placeholder), false) {}
    ~InputText() override;
    template <class Self> Self& onChange(this Self& s, std::function<void(const std::string&)> fn) { s.m_onChange = std::move(fn); return s; }

    const std::string& value() const { return m_value; }
    template <class Self> Self& value(this Self& s, std::string v) { s.setValue(std::move(v)); return s; }

    bool isFocusable() const override { return true; }
    Cursor cursor() const override { return Cursor::IBeam; }
    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onKey(const KeyEvent& e) override;
    bool onChar(uint32_t codepoint) override;
    bool onScroll(float dx, float dy) override; // multi-line text taller than the field
    void onFocus() override;
    void onBlur() override;
    void onLayout() override;
    void onThemeChanged() override;
    void onDraw(SkCanvas* canvas) override;

    // Internal: used by the Yoga measure callback (multi-line only).
    SkSize measure(float width);

protected:
    InputText(std::string placeholder, bool multiline);
    int m_rows = 3;    // multi-line: minimum height in lines
    int m_maxRows = 0; // multi-line: maximum height in lines (0 = grow without limit)

private:
    void setValue(std::string v);
    void edited();
    bool moveCaret(size_t pos, bool extend);
    skia::textlayout::Paragraph& paragraph(float width);
    SkPoint textOrigin(float lineHeight) const;
    SkRect caretRect(); // paragraph-local
    float lineHeight();
    void scrollCaretIntoView();
    void restartBlink(); // caret shown now, then blinking while focused
    void stopBlink();
    size_t hit(float x, float y);

    const bool m_multiline;
    std::string m_placeholder;
    std::string m_value;
    size_t m_caret = 0;  // byte offset into m_value, always on a UTF-8 boundary
    size_t m_anchor = 0; // selection = [min(anchor, caret), max(...)); empty when equal
    bool m_selecting = false;
    bool m_focused = false;
    std::function<void(const std::string&)> m_onChange;
    std::unique_ptr<skia::textlayout::Paragraph> m_paragraph; // cached for current value/placeholder
    float m_layoutWidth = -1.0f;                              // width m_paragraph is laid out at
    float m_scrollX = 0.0f; // single line: text scrolled left by this much to keep the caret in view
    float m_scrollY = 0.0f; // multi-line: text scrolled up by this much when taller than the field
    bool m_followCaret = false; // multi-line: scroll to the caret on the next paint (set by edits and caret moves)
    bool m_caretOn = true;      // blink phase
    int m_blinkTimer = 0;       // Application timer id, 0 = none
};

// Multi-line text field: Enter inserts a newline, text wraps to the width it gets and the field
// grows with its content, from rows() lines up to maxRows() lines (or a maxHeight() / fixed
// height()). Past that the text scrolls inside the field: the wheel scrolls it and the caret is
// kept in view. Up/Down move between lines, Home/End go to the line's start/end.
class TextArea : public InputText {
public:
    explicit TextArea(std::string placeholder = {}) : InputText(std::move(placeholder), true) {}
    TextArea& rows(int n) { m_rows = n; markLayoutDirty(); return *this; }
    TextArea& maxRows(int n) { m_maxRows = n; markLayoutDirty(); return *this; } // 0 = no limit
};

} // namespace Terracotta2
