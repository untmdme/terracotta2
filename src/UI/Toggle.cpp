#include "terracotta2/UI/Toggle.hpp"
#include "terracotta2/UI/Label.hpp"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include "include/core/SkPaint.h"

namespace Terracotta2 {

Toggle::Toggle(bool on) : m_on(on), m_t(on ? 1.0f : 0.0f) { size(44, 24).shrink(0); }

Toggle& Toggle::value(bool on) {
    if (on != m_on) {
        m_on = on;
        m_anim.stop();
        m_t = on ? 1.0f : 0.0f;
        markPaintDirty();
    }
    return *this;
}

void Toggle::flip() {
    m_on = !m_on;
    const float from = m_t, to = m_on ? 1.0f : 0.0f;
    animate(0.15f, [=, this](float k) { m_t = from + (to - from) * k; markPaintDirty(); });
    if (m_onChange) m_onChange(m_on); // last: handler may destroy us
}

bool Toggle::onMouseClick(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    flip();
    return true;
}

bool Toggle::onKey(const KeyEvent& e) {
    if (e.action != KeyAction::Press || (e.key != Key::Space && e.key != Key::Enter)) return false;
    flip();
    return true;
}

void Toggle::onDraw(SkCanvas* c) {
    const float r = m_bounds.height() / 2;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(theme().track);
    c->drawRoundRect(m_bounds, r, r, p);
    p.setColor(SkColorSetA(theme().accent, uint8_t(SkColorGetA(theme().accent) * m_t)));
    c->drawRoundRect(m_bounds, r, r, p);
    p.setColor(SK_ColorWHITE);
    c->drawCircle(m_bounds.left() + r + (m_bounds.width() - 2 * r) * m_t, m_bounds.centerY(), r - 3, p);
    drawFocusRing(c, r);
}

Checkbox::Checkbox(std::string text, bool checked) : m_checked(checked) {
    padding(Edge::Left, 26).minSize(0, 20).justify(Justify::Center);
    add<Label>(std::move(text)); // clicks on the label bubble up to onMouseClick
}

Checkbox& Checkbox::value(bool checked) {
    if (checked != m_checked) { m_checked = checked; markPaintDirty(); }
    return *this;
}

void Checkbox::flip() {
    m_checked = !m_checked;
    markPaintDirty();
    if (m_onChange) m_onChange(m_checked);
}

bool Checkbox::onMouseClick(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    flip();
    return true;
}

bool Checkbox::onKey(const KeyEvent& e) {
    if (e.action != KeyAction::Press || e.key != Key::Space) return false;
    flip();
    return true;
}

void Checkbox::onDraw(SkCanvas* c) {
    const SkRect box = SkRect::MakeXYWH(m_bounds.left() + 1, m_bounds.centerY() - 9, 18, 18);
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(m_checked ? theme().accent : theme().input.background);
    c->drawRoundRect(box, 4, 4, p);
    p.setStyle(SkPaint::kStroke_Style);
    const bool ring = isFocused() && window()->focusVisible();
    p.setStrokeWidth(ring ? 2 : 1);
    p.setColor(m_checked || ring || isHovered() ? theme().accent : theme().input.border);
    c->drawRoundRect(box, 4, 4, p);
    if (!m_checked) return;
    p.setColor(SK_ColorWHITE);
    p.setStrokeWidth(2);
    p.setStrokeCap(SkPaint::kRound_Cap);
    const float x = box.left(), y = box.top();
    c->drawLine(x + 4.5f, y + 9.5f, x + 7.5f, y + 12.5f, p);
    c->drawLine(x + 7.5f, y + 12.5f, x + 13.5f, y + 5.5f, p);
}

} // namespace Terracotta2
