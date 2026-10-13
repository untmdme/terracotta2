#pragma once
#include <functional>
#include <string>
#include "include/core/SkColor.h"
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2 {

class InputText;

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
