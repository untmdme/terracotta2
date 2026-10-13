#include "terracotta2/UI/Slider.hpp"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include <algorithm>
#include "include/core/SkPaint.h"

namespace Terracotta2 {

namespace {
constexpr float kSliderKnob = 8.0f;
}

Slider::Slider(float min, float max) : m_min(min), m_max(max), m_value(min) { height(20).minSize(40, 20); }

Slider& Slider::value(float v) {
    v = std::clamp(v, m_min, m_max);
    if (v != m_value) { m_value = v; markPaintDirty(); }
    return *this;
}

void Slider::edit(float v) {
    const float old = m_value;
    value(v);
    if (m_value != old && m_onChange) m_onChange(m_value);
}

void Slider::dragTo(float x) {
    const float t = (x - m_bounds.left() - kSliderKnob) / std::max(1.0f, m_bounds.width() - 2 * kSliderKnob);
    edit(m_min + (m_max - m_min) * std::clamp(t, 0.0f, 1.0f));
}

bool Slider::onMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    m_drag = true;
    dragTo(e.x);
    return true;
}
bool Slider::onMouseMove(const MouseEvent& e) {
    if (m_drag) dragTo(e.x);
    return m_drag;
}
bool Slider::onMouseUp(const MouseEvent&) {
    m_drag = false;
    return true;
}

bool Slider::onKey(const KeyEvent& e) {
    if (e.action == KeyAction::Release) return false;
    const float step = (m_max - m_min) * ((e.mods & ModShift) ? 0.1f : 0.01f);
    switch (e.key) {
    case Key::Left: case Key::Down: edit(m_value - step); return true;
    case Key::Right: case Key::Up: edit(m_value + step); return true;
    case Key::Home: edit(m_min); return true;
    case Key::End: edit(m_max); return true;
    default: return false;
    }
}

void Slider::onDraw(SkCanvas* c) {
    const float x0 = m_bounds.left() + kSliderKnob, x1 = m_bounds.right() - kSliderKnob, y = m_bounds.centerY();
    const float t = m_max > m_min ? (m_value - m_min) / (m_max - m_min) : 0.0f;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(theme().track);
    c->drawRoundRect(SkRect::MakeLTRB(x0, y - 2, x1, y + 2), 2, 2, p);
    p.setColor(theme().accent);
    c->drawRoundRect(SkRect::MakeLTRB(x0, y - 2, x0 + (x1 - x0) * t, y + 2), 2, 2, p);
    p.setColor(SK_ColorWHITE);
    c->drawCircle(x0 + (x1 - x0) * t, y, kSliderKnob - 1, p);
    if (isFocused() && window()->focusVisible()) {
        p.setStyle(SkPaint::kStroke_Style);
        p.setStrokeWidth(2);
        p.setColor(theme().accent);
        c->drawCircle(x0 + (x1 - x0) * t, y, kSliderKnob - 1, p);
    }
}

ProgressBar& ProgressBar::value(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    if (v != m_value) { m_value = v; markPaintDirty(); }
    return *this;
}

void ProgressBar::onDraw(SkCanvas* c) {
    const float r = m_bounds.height() / 2;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(theme().track);
    c->drawRoundRect(m_bounds, r, r, p);
    p.setColor(theme().accent);
    c->drawRoundRect(SkRect::MakeXYWH(m_bounds.left(), m_bounds.top(), m_bounds.width() * m_value, m_bounds.height()), r, r, p);
}

} // namespace Terracotta2
