#include "terracotta2/UI/DragNumber.hpp"
#include "terracotta2/UI/Label.hpp"
#include "terracotta2/Theme/Theme.hpp"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"

namespace Terracotta2 {

DragNumber::DragNumber(std::function<float()> get, std::function<void(float)> set, float min, float max,
                       float perPixel, int decimals)
    : m_get(std::move(get)), m_set(std::move(set)), m_min(min), m_max(max), m_perPixel(perPixel), m_decimals(decimals) {
    height(theme().input.height).justify(Justify::Center).alignItems(Align::Center).shrink(0.0f);
    m_label = &add<Label>("");
    refresh();
}

void DragNumber::refresh() {
    const float v = m_get();
    char buf[32];
    if (std::isnan(v)) std::snprintf(buf, sizeof buf, "auto");
    else std::snprintf(buf, sizeof buf, "%.*f", m_decimals, v);
    if (m_label->text() != buf) m_label->text(buf);
    markPaintDirty(); // the fill
}

bool DragNumber::onMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return false;
    m_drag = true;
    m_grabX = e.x;
    m_grabValue = !std::isnan(m_get()) ? m_get() : m_auto ? m_auto() : 0.0f;
    return true;
}

bool DragNumber::onMouseMove(const MouseEvent& e) {
    if (!m_drag) return false;
    m_set(std::clamp(m_grabValue + (e.x - m_grabX) * m_perPixel, m_min, m_max));
    refresh();
    return true;
}

bool DragNumber::onMouseUp(const MouseEvent&) {
    m_drag = false;
    return true;
}

void DragNumber::onDraw(SkCanvas* canvas) {
    const auto& st = theme().input;
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(st.background);
    canvas->drawRoundRect(m_bounds, st.cornerRadius, st.cornerRadius, p);
    const float v = m_get();
    if (!std::isnan(v) && m_max > m_min) {
        canvas->save();
        canvas->clipRRect(SkRRect::MakeRectXY(m_bounds, st.cornerRadius, st.cornerRadius), true);
        SkRect fill = m_bounds;
        fill.fRight = fill.fLeft + fill.width() * std::clamp((v - m_min) / (m_max - m_min), 0.0f, 1.0f);
        p.setColor(SkColorSetA(theme().accent, 90));
        canvas->drawRect(fill, p);
        canvas->restore();
    }
}

} // namespace Terracotta2
