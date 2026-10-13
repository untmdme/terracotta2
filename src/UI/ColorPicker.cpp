#include "terracotta2/UI/ColorPicker.hpp"
#include "terracotta2/UI/InputText.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>
#include <cctype>
#include <cmath>
#include "include/core/SkPaint.h"
#include "include/effects/SkGradient.h"

namespace Terracotta2 {

namespace {
constexpr float kPickW = 240, kSVH = 150, kBar = 16, kPickGap = 8;

void drawChecker(SkCanvas* c, const SkRect& r) {
    SkPaint p;
    p.setColor(SkColorSetRGB(200, 200, 200));
    c->drawRect(r, p);
    p.setColor(SkColorSetRGB(150, 150, 150));
    const float s = 6;
    for (float y = r.top(); y < r.bottom(); y += s)
        for (float x = r.left() + (int((y - r.top()) / s) % 2) * s; x < r.right(); x += 2 * s)
            c->drawRect(SkRect::MakeLTRB(x, y, std::min(x + s, r.right()), std::min(y + s, r.bottom())), p);
}

sk_sp<SkShader> linear(SkPoint a, SkPoint b, std::initializer_list<SkColor> colors) {
    std::vector<SkColor4f> stops;
    for (SkColor c : colors) stops.push_back(SkColor4f::FromColor(c));
    SkPoint pts[] = {a, b};
    return SkShaders::LinearGradient(
        pts, SkGradient(SkGradient::Colors({stops.data(), stops.size()}, SkTileMode::kClamp), SkGradient::Interpolation()));
}
} // namespace

std::string colorToHex(SkColor c) {
    char buf[16];
    if (SkColorGetA(c) == 255) std::snprintf(buf, sizeof buf, "#%02X%02X%02X", SkColorGetR(c), SkColorGetG(c), SkColorGetB(c));
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", SkColorGetR(c), SkColorGetG(c), SkColorGetB(c), SkColorGetA(c));
    return buf;
}

bool hexToColor(const std::string& hex, SkColor& out) {
    std::string h = hex.starts_with('#') ? hex.substr(1) : hex;
    if (h.size() == 3) h = {h[0], h[0], h[1], h[1], h[2], h[2]};
    if ((h.size() != 6 && h.size() != 8) || !std::all_of(h.begin(), h.end(), [](char ch) { return std::isxdigit(uint8_t(ch)); }))
        return false;
    const uint32_t v = uint32_t(std::stoul(h, nullptr, 16));
    out = h.size() == 6 ? (0xFF000000u | v) : ((v & 0xFF) << 24 | v >> 8); // RRGGBBAA -> ARGB
    return true;
}

ColorPicker::ColorPicker(std::function<SkColor()> get, std::function<void(SkColor)> set)
    : m_get(std::move(get)), m_set(std::move(set)) {
    width(kPickW).padding(Edge::Top, kSVH + kBar + 2 * kPickGap).shrink(0.0f); // the hex field sits below the drawn parts
    m_hex = &add<InputText>("#RRGGBB").onChange([this](const std::string& text) {
        SkColor c;
        if (!hexToColor(text, c)) return;
        m_shown = c;
        m_alpha = SkColorGetA(c);
        SkColorToHSV(c, m_hsv);
        m_set(c);
        markPaintDirty();
    });
    refresh();
}

void ColorPicker::refresh() {
    const SkColor c = m_get();
    if (c == m_shown) return;
    m_shown = c;
    m_alpha = SkColorGetA(c);
    float hsv[3];
    SkColorToHSV(c, hsv);
    if (hsv[1] > 0 && hsv[2] > 0) m_hsv[0] = hsv[0]; // grey/black have no hue: keep the old one
    if (hsv[2] > 0) m_hsv[1] = hsv[1];
    m_hsv[2] = hsv[2];
    m_hex->value(colorToHex(c));
    markPaintDirty();
}

void ColorPicker::apply() {
    m_shown = SkHSVToColor(m_alpha, m_hsv);
    m_set(m_shown);
    m_hex->value(colorToHex(m_shown));
    markPaintDirty();
}

SkRect ColorPicker::svRect() const {
    return SkRect::MakeXYWH(m_bounds.left(), m_bounds.top(), m_bounds.width() - kBar - kPickGap, kSVH);
}
SkRect ColorPicker::hueRect() const { return SkRect::MakeXYWH(m_bounds.right() - kBar, m_bounds.top(), kBar, kSVH); }
SkRect ColorPicker::alphaRect() const {
    return SkRect::MakeXYWH(m_bounds.left(), m_bounds.top() + kSVH + kPickGap, m_bounds.width(), kBar);
}

ColorPicker::Part ColorPicker::partAt(float x, float y) const {
    if (svRect().contains(x, y)) return Part::SV;
    if (hueRect().contains(x, y)) return Part::Hue;
    if (alphaRect().contains(x, y)) return Part::Alpha;
    return Part::None;
}

void ColorPicker::dragTo(float x, float y) {
    auto unit = [](float v, float a, float b) { return std::clamp((v - a) / (b - a), 0.0f, 1.0f); };
    if (m_drag == Part::SV) {
        const SkRect r = svRect();
        m_hsv[1] = unit(x, r.left(), r.right());
        m_hsv[2] = 1.0f - unit(y, r.top(), r.bottom());
    } else if (m_drag == Part::Hue) {
        const SkRect r = hueRect();
        m_hsv[0] = std::min(359.99f, 360.0f * unit(y, r.top(), r.bottom()));
    } else if (m_drag == Part::Alpha) {
        const SkRect r = alphaRect();
        m_alpha = uint8_t(std::lround(255 * unit(x, r.left(), r.right())));
    }
    apply();
}

bool ColorPicker::onMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left || (m_drag = partAt(e.x, e.y)) == Part::None) return false;
    dragTo(e.x, e.y);
    return true;
}
bool ColorPicker::onMouseMove(const MouseEvent& e) {
    if (m_drag == Part::None) return false;
    dragTo(e.x, e.y);
    return true;
}
bool ColorPicker::onMouseUp(const MouseEvent&) {
    m_drag = Part::None;
    return true;
}

void ColorPicker::onDraw(SkCanvas* c) {
    SkPaint p;
    p.setAntiAlias(true);
    // Saturation/value: the pure hue, white fading in from the left, black from the bottom.
    const SkRect sv = svRect();
    p.setColor(SkHSVToColor(255, std::array<float, 3>{m_hsv[0], 1, 1}.data()));
    c->drawRect(sv, p);
    p.setShader(linear({sv.left(), 0}, {sv.right(), 0}, {SK_ColorWHITE, SkColorSetARGB(0, 255, 255, 255)}));
    c->drawRect(sv, p);
    p.setShader(linear({0, sv.top()}, {0, sv.bottom()}, {SkColorSetARGB(0, 0, 0, 0), SK_ColorBLACK}));
    c->drawRect(sv, p);
    // Hue: red -> yellow -> green -> cyan -> blue -> magenta -> red, top to bottom.
    const SkRect hue = hueRect();
    p.setShader(linear({0, hue.top()}, {0, hue.bottom()},
                       {SK_ColorRED, SK_ColorYELLOW, SK_ColorGREEN, SK_ColorCYAN, SK_ColorBLUE, SK_ColorMAGENTA, SK_ColorRED}));
    c->drawRect(hue, p);
    // Alpha: the color from transparent to opaque over a checkerboard.
    const SkRect al = alphaRect();
    drawChecker(c, al);
    const SkColor opaque = SkHSVToColor(255, m_hsv);
    p.setShader(linear({al.left(), 0}, {al.right(), 0}, {SkColorSetA(opaque, 0), opaque}));
    c->drawRect(al, p);
    p.setShader(nullptr);

    // Markers: ring on the SV square, bars across hue and alpha. Clipped: they must not leave our bounds.
    c->save();
    c->clipRect(m_bounds);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(2);
    p.setColor(SK_ColorWHITE);
    c->drawCircle(sv.left() + m_hsv[1] * sv.width(), sv.top() + (1 - m_hsv[2]) * sv.height(), 5, p);
    const float hy = hue.top() + m_hsv[0] / 360.0f * hue.height();
    c->drawRect(SkRect::MakeLTRB(hue.left() - 1, hy - 2, hue.right() + 1, hy + 2), p);
    const float ax = al.left() + m_alpha / 255.0f * al.width();
    c->drawRect(SkRect::MakeLTRB(ax - 2, al.top() - 1, ax + 2, al.bottom() + 1), p);
    c->restore();
}

} // namespace Terracotta2
