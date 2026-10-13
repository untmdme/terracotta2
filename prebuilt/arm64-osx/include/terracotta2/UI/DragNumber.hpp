#pragma once
#include <functional>
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2 {

class Label;

// Number edited by dragging horizontally, like imgui's DragFloat; the filled part shows where the
// value sits in [min, max]. Reads and writes through get/set, so it can edit any value.
// NaN from get() shows "auto" (unset Yoga sizes); a drag then starts from 0.
class DragNumber : public Widget {
public:
    DragNumber(std::function<float()> get, std::function<void(float)> set, float min, float max,
               float perPixel = 1.0f, int decimals = 0);
    void refresh(); // re-read get(), e.g. after the value changed elsewhere
    // Where a drag starts while get() is NaN ("auto"), e.g. the widget's current size. Default 0.
    DragNumber& whenAuto(std::function<float()> fn) { m_auto = std::move(fn); return *this; }

    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    Cursor cursor() const override { return Cursor::HResize; }
    void onThemeChanged() override { refresh(); }
    void onDraw(SkCanvas* canvas) override;

private:
    std::function<float()> m_get;
    std::function<void(float)> m_set;
    std::function<float()> m_auto;
    float m_min, m_max, m_perPixel;
    int m_decimals;
    Label* m_label;
    float m_grabX = 0, m_grabValue = 0;
    bool m_drag = false;
};

} // namespace Terracotta2
