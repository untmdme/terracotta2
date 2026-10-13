#include "terracotta2/UI/Label.hpp"
#include "Text.hpp"
#include "terracotta2/Core/Application.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace Terracotta2 {

using namespace detail;

namespace {
YGSize measureLabel(YGNodeConstRef node, float width, YGMeasureMode wm, float, YGMeasureMode) {
    auto* label = static_cast<Label*>(YGNodeGetContext(node));
    SkSize s = label->measure(wm == YGMeasureModeUndefined ? SK_ScalarInfinity : width);
    return {s.width(), s.height()};
}
} // namespace

Label::Label(std::string text, float fontSize, int weight)
    : m_text(std::move(text)),
      m_fontSize(fontSize > 0 ? fontSize : theme().label.fontSize),
      m_weight(weight),
      m_color(theme().label.text),
      m_themeSize(fontSize <= 0) {
    YGNodeSetMeasureFunc(yogaNode(), measureLabel);
}

void Label::onThemeChanged() {
    if (m_themeSize) m_fontSize = theme().label.fontSize;
    if (m_themeColor) m_color = theme().label.text;
    m_paragraph.reset(); // theme().fontFamily may have changed too
    markLayoutDirty();
}

Label::~Label() = default;

void Label::restyle() {
    m_paragraph.reset();
    markLayoutDirty();
    markPaintDirty(); // the size may be unchanged while the content did change
}

Label& Label::text(std::string text) {
    m_text = std::move(text);
    m_selAnchor = m_selEnd = 0;
    restyle();
    return *this;
}
Label& Label::color(SkColor color) { // same size: reshape and repaint, no layout
    m_color = color;
    m_themeColor = false;
    m_paragraph.reset();
    markPaintDirty();
    return *this;
}
Label& Label::fontSize(float size) {
    m_fontSize = size > 0 ? size : theme().label.fontSize;
    m_themeSize = size <= 0;
    restyle();
    return *this;
}
Label& Label::weight(int weight) { m_weight = weight; restyle(); return *this; }
Label& Label::lineHeight(float multiplier) { m_lineHeight = multiplier; restyle(); return *this; }
Label& Label::font(std::string family) { m_family = std::move(family); restyle(); return *this; }

Paragraph& Label::paragraph(float width) {
    if (!m_paragraph) {
        m_paragraph = makeParagraph(m_text, m_fontSize, m_color, m_weight, m_lineHeight, m_family);
        m_layoutWidth = -1.0f;
    }
    if (m_layoutWidth != width) {
        m_paragraph->layout(width);
        m_layoutWidth = width;
    }
    return *m_paragraph;
}

SkSize Label::measure(float maxWidth) {
    Paragraph& p = paragraph(maxWidth);
    const float intrinsic = std::ceil(p.getMaxIntrinsicWidth());
    return SkSize::Make(std::isinf(maxWidth) ? intrinsic : std::min(maxWidth, intrinsic), std::ceil(p.getHeight()));
}

void Label::onDraw(SkCanvas* canvas) {
    Paragraph& p = paragraph(m_bounds.width());
    if (m_selAnchor != m_selEnd) paintSelection(canvas, p, m_text, m_selAnchor, m_selEnd, m_bounds.left(), m_bounds.top());
    p.paint(canvas, m_bounds.left(), m_bounds.top());
}

Label& Label::selectable(bool on) {
    if (m_selectable == on) return *this;
    m_selectable = on;
    m_selecting = false;
    if (!on && m_selAnchor != m_selEnd) {
        m_selAnchor = m_selEnd = 0;
        markPaintDirty();
    }
    return *this;
}

std::string Label::selectedText() const {
    size_t lo = std::min(m_selAnchor, m_selEnd), hi = std::min(std::max(m_selAnchor, m_selEnd), m_text.size());
    return lo < hi ? m_text.substr(lo, hi - lo) : std::string();
}

bool Label::onMouseDown(const MouseEvent& e) {
    if (!m_selectable || e.button != MouseButton::Left) return false; // bubble to the parent as before
    size_t off = hitOffset(paragraph(m_bounds.width()), m_text, e.x - m_bounds.left(), e.y - m_bounds.top());
    size_t a = off, b = off;
    if (isDoubleClick(this, e.x, e.y)) std::tie(a, b) = wordRange(m_text, off);
    if (a != m_selAnchor || b != m_selEnd) {
        m_selAnchor = a;
        m_selEnd = b;
        markPaintDirty();
    }
    m_selecting = true;
    return true;
}

bool Label::onMouseMove(const MouseEvent& e) {
    if (!m_selecting) return false;
    size_t off = hitOffset(*m_paragraph, m_text, e.x - m_bounds.left(), e.y - m_bounds.top());
    if (off != m_selEnd) {
        m_selEnd = off;
        markPaintDirty();
    }
    return true;
}

bool Label::onMouseUp(const MouseEvent&) {
    bool was = m_selecting;
    m_selecting = false;
    return was;
}

bool Label::onKey(const KeyEvent& e) {
    if (!m_selectable || e.action == KeyAction::Release || !(e.mods & (ModSuper | ModCtrl))) return false;
    if (e.key == Key::A) {
        if (m_selAnchor != 0 || m_selEnd != m_text.size()) {
            m_selAnchor = 0;
            m_selEnd = m_text.size();
            markPaintDirty();
        }
        return true;
    }
    if (e.key == Key::C) {
        std::string sel = selectedText();
        if (!sel.empty()) Application::get().setClipboard(sel);
        return true;
    }
    return false;
}

void Label::onBlur() {
    m_selecting = false;
    if (m_selAnchor != m_selEnd) {
        m_selAnchor = m_selEnd = 0;
        markPaintDirty();
    }
}

} // namespace Terracotta2
