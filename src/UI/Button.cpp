#include "terracotta2/UI/Button.hpp"
#include "Text.hpp"
#include "terracotta2/UI/Label.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include "include/core/SkPaint.h"

namespace Terracotta2 {

using namespace detail;

Button::Button(std::string text) {
    const auto& st = theme().button;
    direction(Direction::Row).alignItems(Align::Center)
        .padding(Edge::Horizontal, st.paddingX).padding(Edge::Vertical, st.paddingY)
        .alignSelf(Align::Start);
    m_label = &add<Label>(std::move(text)).color(st.text);
}

void Button::onThemeChanged() {
    const auto& st = theme().button;
    padding(Edge::Horizontal, st.paddingX).padding(Edge::Vertical, st.paddingY);
    m_label->color(st.text);
}

bool Button::onMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    m_pressed = true;
    markPaintDirty();
    return true;
}

bool Button::onMouseUp(const MouseEvent&) {
    if (!m_pressed) return false;
    m_pressed = false;
    markPaintDirty();
    return true;
}

bool Button::onMouseClick(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    if (m_onClick) m_onClick(); // last: handler may destroy us
    return true;
}

bool Button::onKey(const KeyEvent& e) {
    if (e.action != KeyAction::Press || (e.key != Key::Space && e.key != Key::Enter)) return false;
    if (m_onClick) m_onClick();
    return true;
}

void Button::onMouseEnter() {
    float from = m_hover;
    animate(theme().button.hoverFadeSec, [this, from](float t) {
        m_hover = from + (1.0f - from) * t;
        markPaintDirty();
    });
}

void Button::onMouseLeave() {
    float from = m_hover;
    animate(theme().button.hoverFadeSec, [this, from](float t) {
        m_hover = from * (1.0f - t);
        markPaintDirty();
    });
}

void Button::onDraw(SkCanvas* canvas) {
    const auto& st = theme().button;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(m_pressed ? st.activeBackground : lerpColor(st.background, st.hoverBackground, m_hover));
    canvas->drawRoundRect(m_bounds, st.cornerRadius, st.cornerRadius, p);
    drawFocusRing(canvas, st.cornerRadius);
}

} // namespace Terracotta2
