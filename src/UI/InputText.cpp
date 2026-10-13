#include "terracotta2/UI/InputText.hpp"
#include "Text.hpp"
#include "terracotta2/UI/ScrollView.hpp"
#include "terracotta2/Core/Application.hpp"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include <algorithm>
#include <vector>
#include <cmath>
#include <tuple>
#include "include/core/SkPaint.h"

namespace Terracotta2 {

using namespace detail;

namespace {
YGSize measureInput(YGNodeConstRef node, float width, YGMeasureMode wm, float, YGMeasureMode) {
    auto* in = static_cast<InputText*>(YGNodeGetContext(node));
    SkSize s = in->measure(wm == YGMeasureModeUndefined ? 240.0f : width);
    return {s.width(), s.height()};
}
} // namespace

InputText::InputText(std::string placeholder, bool multiline)
    : m_multiline(multiline), m_placeholder(std::move(placeholder)) {
    padding(Edge::Horizontal, theme().input.paddingX).shrink(0.0f);
    if (m_multiline) {
        padding(Edge::Vertical, theme().input.paddingX);
        YGNodeSetContext(yogaNode(), this);
        YGNodeSetMeasureFunc(yogaNode(), measureInput);
    } else {
        height(theme().input.height);
    }
}

InputText::~InputText() = default;

void InputText::onThemeChanged() {
    padding(Edge::Horizontal, theme().input.paddingX);
    if (m_multiline) padding(Edge::Vertical, theme().input.paddingX);
    else height(theme().input.height);
    m_paragraph.reset();
    markLayoutDirty();
}

// Single line: laid out at infinite width so it never wraps; the draw clip handles overflow.
Paragraph& InputText::paragraph(float width) {
    if (!m_multiline) width = SK_ScalarInfinity;
    if (!m_paragraph) {
        const auto& st = theme();
        const bool empty = m_value.empty();
        m_paragraph = makeParagraph(empty ? m_placeholder : m_value, st.label.fontSize, empty ? st.input.placeholder : st.input.text);
        m_layoutWidth = -1.0f;
    }
    if (width != m_layoutWidth) {
        m_paragraph->layout(width);
        m_layoutWidth = width;
    }
    return *m_paragraph;
}

float InputText::lineHeight() {
    std::vector<skia::textlayout::LineMetrics> lines;
    paragraph(std::max(1.0f, m_bounds.width() - 2 * theme().input.paddingX)).getLineMetrics(lines);
    return lines.empty() ? theme().label.fontSize * 1.2f : float(lines[0].fHeight);
}

// Yoga passes the content width (inside the padding) and adds the padding to what we return.
SkSize InputText::measure(float width) {
    const float lineH = lineHeight();
    float h = std::max(paragraph(std::max(1.0f, width)).getHeight(), m_rows * lineH);
    if (m_maxRows > 0) h = std::min(h, std::max(m_rows, m_maxRows) * lineH);
    return {width, h};
}

SkPoint InputText::textOrigin(float lineH) const {
    const float pad = theme().input.paddingX;
    if (m_multiline) return {m_bounds.left() + pad, m_bounds.top() + pad - m_scrollY};
    return {m_bounds.left() + pad - m_scrollX, m_bounds.top() + (m_bounds.height() - lineH) * 0.5f};
}

// The visual line holding UTF-16 offset u: the one with start <= u < end-including-newline, else
// the last line (caret at the very end, including the empty line skparagraph adds after a
// trailing newline).
static const skia::textlayout::LineMetrics& lineAt(const std::vector<skia::textlayout::LineMetrics>& lines, size_t u) {
    for (const auto& l : lines)
        if (l.fStartIndex <= u && u < l.fEndIncludingNewline) return l;
    return lines.back();
}

static float paragraphWidth(const SkRect& bounds) { return std::max(1.0f, bounds.width() - 2 * theme().input.paddingX); }

// Caret box (1 px wide, line height tall) relative to the paragraph origin.
SkRect InputText::caretRect() {
    Paragraph& p = paragraph(paragraphWidth(m_bounds));
    std::vector<skia::textlayout::LineMetrics> lines;
    p.getLineMetrics(lines);
    const float fallbackH = theme().label.fontSize * 1.2f;
    if (m_value.empty() || lines.empty()) return SkRect::MakeXYWH(0, 0, 1, lines.empty() ? fallbackH : float(lines[0].fHeight));
    const size_t u = toUtf16(m_value, m_caret);
    const skia::textlayout::LineMetrics* line = &lineAt(lines, u);
    const float top = float(line->fBaseline - line->fAscent), h = float(line->fHeight);
    // After a trailing newline the caret starts the empty last line.
    if (m_caret == m_value.size() && m_value.back() == '\n') return SkRect::MakeXYWH(0, top, 1, h);
    float x = float(line->fLeft);
    if (toByteOffset(m_value, line->fStartIndex) < m_caret) {
        auto boxes = p.getRectsForRange(unsigned(toUtf16(m_value, prevBoundary(m_value, m_caret))), unsigned(u),
                                        RectHeightStyle::kTight, RectWidthStyle::kTight);
        if (!boxes.empty()) x = boxes.back().rect.fRight;
    }
    return SkRect::MakeXYWH(x, top, 1, h);
}

void InputText::setValue(std::string v) {
    m_value = std::move(v);
    m_caret = m_anchor = m_value.size();
    m_scrollX = m_scrollY = 0.0f;
    m_paragraph.reset();
    if (m_multiline) markLayoutDirty();
    markPaintDirty();
}

void InputText::edited() {
    m_paragraph.reset();
    if (m_multiline) markLayoutDirty(); // the height may change
    m_followCaret = true;
    markPaintDirty();
    scrollCaretIntoView();
    if (m_onChange) m_onChange(m_value);
}

// Moves the caret (extending the selection when `extend`) and repaints only if something changed.
bool InputText::moveCaret(size_t pos, bool extend) {
    const size_t anchor = extend ? m_anchor : pos;
    if (pos != m_caret || anchor != m_anchor) {
        m_caret = pos;
        m_anchor = anchor;
        m_followCaret = true;
        markPaintDirty();
        scrollCaretIntoView();
    }
    return true;
}

void InputText::scrollCaretIntoView() {
    if (!m_focused) return;
    const float lineH = lineHeight();
    const SkPoint o = textOrigin(lineH);
    const SkRect r = caretRect().makeOffset(o.x(), o.y()).makeOutset(0, theme().input.paddingX);
    for (Widget* p = parent(); p; p = p->parent())
        if (auto* sv = dynamic_cast<ScrollView*>(p)) sv->scrollToShow(r);
}

void InputText::onLayout() { scrollCaretIntoView(); } // the field grew while typing at its end

size_t InputText::hit(float x, float y) {
    if (m_value.empty()) return 0;
    const float lineH = lineHeight();
    const SkPoint o = textOrigin(lineH);
    Paragraph& p = paragraph(paragraphWidth(m_bounds));
    return hitOffset(p, m_value, x - o.x(), m_multiline ? y - o.y() : lineH * 0.5f);
}

bool InputText::onMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    size_t off = hit(e.x, e.y);
    size_t anchor = m_anchor, caret = off;
    if (isDoubleClick(this, e.x, e.y)) std::tie(anchor, caret) = wordRange(m_value, off);
    else if (!(e.mods & ModShift)) anchor = off;
    if (anchor != m_anchor || caret != m_caret) {
        m_anchor = anchor;
        m_caret = caret;
        markPaintDirty();
    }
    m_selecting = true;
    return true;
}

bool InputText::onMouseMove(const MouseEvent& e) {
    if (!m_selecting) return false;
    moveCaret(hit(e.x, e.y), true);
    return true;
}

bool InputText::onMouseUp(const MouseEvent&) {
    bool was = m_selecting;
    m_selecting = false;
    return was;
}

bool InputText::onScroll(float, float dy) {
    if (!m_multiline) return false;
    const float before = m_scrollY;
    const float visible = m_bounds.height() - 2 * theme().input.paddingX;
    const float max = std::max(0.0f, paragraph(paragraphWidth(m_bounds)).getHeight() - visible);
    m_scrollY = std::clamp(m_scrollY - dy * theme().scroll.wheelSpeed, 0.0f, max);
    if (m_scrollY == before) return false; // at an end: let a parent ScrollView take it
    m_followCaret = false; // the user scrolled away on purpose
    markPaintDirty();
    return true;
}

bool InputText::onChar(uint32_t cp) {
    if (cp < 32 || cp == 127) return false;
    replaceSelection(m_value, m_caret, m_anchor, encodeUtf8(cp));
    edited();
    return true;
}

bool InputText::onKey(const KeyEvent& e) {
    if (e.action == KeyAction::Release) return false;
    const bool shift = e.mods & ModShift, cmd = e.mods & (ModSuper | ModCtrl);
    // Word moves: Option+arrow (macOS) or Ctrl+arrow (Windows). Line moves: Cmd+arrow (macOS).
    const bool word = e.mods & (ModAlt | ModCtrl), line = e.mods & ModSuper;
    const size_t lo = std::min(m_caret, m_anchor), hi = std::max(m_caret, m_anchor);
    auto moveTo = [&](size_t pos) { return moveCaret(pos, shift); };
    auto copy = [&] { if (lo != hi) Application::get().setClipboard(m_value.substr(lo, hi - lo)); };
    auto insert = [&](const std::string& s) {
        replaceSelection(m_value, m_caret, m_anchor, s);
        edited();
        return true;
    };
    // Multi-line: the byte range [start, end) of the caret's visual line (end before trailing spaces/newline).
    auto lineRange = [&]() -> std::pair<size_t, size_t> {
        std::vector<skia::textlayout::LineMetrics> lines;
        paragraph(paragraphWidth(m_bounds)).getLineMetrics(lines);
        if (lines.empty() || m_value.empty()) return {0, m_value.size()};
        const skia::textlayout::LineMetrics* line = &lineAt(lines, toUtf16(m_value, m_caret));
        const size_t a = toByteOffset(m_value, line->fStartIndex);
        if (m_caret == m_value.size() && m_value.back() == '\n' && a < m_caret) return {m_caret, m_caret}; // empty last line
        return {a, std::max(a, toByteOffset(m_value, line->fEndExcludingWhitespaces))};
    };
    // Multi-line: the offset one line above (-1) or below (+1) the caret, at the same x.
    auto verticalMove = [&](int dir) {
        const SkRect c = caretRect();
        const float y = dir < 0 ? c.top() - c.height() * 0.5f : c.bottom() + c.height() * 0.5f;
        if (y < 0) return size_t(0);
        if (y > paragraph(paragraphWidth(m_bounds)).getHeight()) return m_value.size();
        return hitOffset(*m_paragraph, m_value, c.left(), y);
    };
    switch (e.key) {
    case Key::Left:
        if (line) return moveTo(m_multiline ? lineRange().first : 0);
        if (word) return moveTo(prevWord(m_value, m_caret));
        return (lo != hi && !shift) ? moveTo(lo) : moveTo(prevBoundary(m_value, m_caret));
    case Key::Right:
        if (line) return moveTo(m_multiline ? lineRange().second : m_value.size());
        if (word) return moveTo(nextWord(m_value, m_caret));
        return (lo != hi && !shift) ? moveTo(hi) : moveTo(nextBoundary(m_value, m_caret));
    case Key::Up:    return m_multiline ? moveTo(line ? 0 : verticalMove(-1)) : false;
    case Key::Down:  return m_multiline ? moveTo(line ? m_value.size() : verticalMove(+1)) : false;
    case Key::Home:  return moveTo(m_multiline && !cmd ? lineRange().first : 0);
    case Key::End:   return moveTo(m_multiline && !cmd ? lineRange().second : m_value.size());
    case Key::Enter: return m_multiline ? insert("\n") : false;
    case Key::Backspace: // Option/Ctrl: the word before; Cmd: to the line start
        if (lo == hi) m_anchor = line ? (m_multiline ? lineRange().first : 0) : word ? prevWord(m_value, m_caret) : prevBoundary(m_value, m_caret);
        if (m_anchor == m_caret) return true; // nothing before the caret
        return insert("");
    case Key::Delete:
        if (lo == hi) m_anchor = word ? nextWord(m_value, m_caret) : nextBoundary(m_value, m_caret);
        if (m_anchor == m_caret) return true;
        return insert("");
    case Key::A:
        if (!cmd) return false;
        if (m_anchor != 0 || m_caret != m_value.size()) {
            m_anchor = 0;
            m_caret = m_value.size();
            markPaintDirty();
        }
        return true;
    case Key::C:
        if (!cmd) return false;
        copy();
        return true;
    case Key::X:
        if (!cmd) return false;
        if (lo == hi) return true;
        copy();
        return insert("");
    case Key::V: {
        if (!cmd) return false;
        std::string clip = Application::get().clipboard();
        clip.erase(std::remove_if(clip.begin(), clip.end(), [&](char c) { return c == '\r' || (c == '\n' && !m_multiline); }), clip.end());
        return clip.empty() ? true : insert(clip);
    }
    case Key::Escape:
        if (Window* w = window()) w->setFocus(nullptr);
        return true;
    default: return false;
    }
}

void InputText::onFocus() { m_focused = true; markPaintDirty(); }
void InputText::onBlur() {
    m_focused = false;
    m_selecting = false;
    m_anchor = m_caret;
    m_scrollX = 0.0f; // show the start again
    markPaintDirty();
}

void InputText::onDraw(SkCanvas* canvas) {
    const auto& st = theme().input;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(st.background);
    canvas->drawRoundRect(m_bounds, st.cornerRadius, st.cornerRadius, p);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(1.0f);
    p.setColor(m_focused ? st.focusBorder : st.border);
    canvas->drawRoundRect(m_bounds.makeInset(0.5f, 0.5f), st.cornerRadius, st.cornerRadius, p);

    Paragraph& para = paragraph(paragraphWidth(m_bounds));
    if (!m_multiline && m_focused) {
        // Scroll just enough to keep the caret inside the field (a drag past an edge scrolls on as
        // the mouse moves), and never past the end of the text. Hit tests use the same offset.
        const float visible = paragraphWidth(m_bounds) - 1.0f, cx = caretRect().left();
        if (cx - m_scrollX > visible) m_scrollX = cx - visible;
        if (cx < m_scrollX) m_scrollX = cx;
        m_scrollX = std::clamp(m_scrollX, 0.0f, std::max(0.0f, para.getLongestLine() - visible));
    }
    float textH = 0, visibleH = 0;
    if (m_multiline) {
        // Text taller than the field scrolls inside it. Follow the caret only after it moved or the
        // text changed, so wheel-scrolling away from the caret sticks.
        textH = para.getHeight();
        visibleH = m_bounds.height() - 2 * st.paddingX;
        if (m_followCaret && m_focused) {
            const SkRect c = caretRect();
            if (c.bottom() - m_scrollY > visibleH) m_scrollY = c.bottom() - visibleH;
            if (c.top() < m_scrollY) m_scrollY = c.top();
        }
        m_followCaret = false;
        m_scrollY = std::clamp(m_scrollY, 0.0f, std::max(0.0f, textH - visibleH));
    }
    const SkPoint o = textOrigin(lineHeight());

    canvas->save();
    canvas->clipRect(m_bounds.makeInset(st.paddingX, 1.0f));
    if (m_focused && !m_value.empty()) paintSelection(canvas, para, m_value, m_anchor, m_caret, o.x(), o.y());
    para.paint(canvas, o.x(), o.y());
    if (m_focused) {
        const SkRect c = caretRect();
        SkPaint cp;
        cp.setColor(st.caret);
        canvas->drawRect(SkRect::MakeXYWH(std::floor(o.x() + c.left()), o.y() + c.top(), 1.0f, c.height()), cp);
    }
    // ponytail: no caret blink (zero idle frames); blink via animate() if wanted
    canvas->restore();
    if (textH > visibleH && visibleH > 0) { // scroll position indicator, like ScrollView's thumb
        const auto& sc = theme().scroll;
        const float track = m_bounds.height() - 2 * sc.margin;
        const float thumb = std::max(16.0f, track * visibleH / textH);
        const float top = m_bounds.top() + sc.margin + (track - thumb) * (m_scrollY / (textH - visibleH));
        SkPaint tp;
        tp.setAntiAlias(true);
        tp.setColor(sc.thumb);
        canvas->drawRoundRect(SkRect::MakeXYWH(m_bounds.right() - sc.margin - sc.thickness, top, sc.thickness, thumb),
                              sc.thickness / 2, sc.thickness / 2, tp);
    }
}

} // namespace Terracotta2
