#pragma once
#include <functional>
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2 {

// Horizontal slider over [min, max]. Drag or click to set; Left/Right step 1% (Shift: 10%).
class Slider : public Widget {
public:
    explicit Slider(float min = 0.0f, float max = 1.0f);
    Slider& onChange(std::function<void(float)> fn) { m_onChange = std::move(fn); return *this; }
    float value() const { return m_value; }
    Slider& value(float v); // clamped to [min, max]

    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onKey(const KeyEvent& e) override;
    bool isFocusable() const override { return true; }
    Cursor cursor() const override { return Cursor::Hand; }
    void onDraw(SkCanvas* canvas) override;

private:
    void edit(float v); // user change: set + onChange
    void dragTo(float x);
    float m_min, m_max, m_value;
    bool m_drag = false;
    std::function<void(float)> m_onChange;
};

// Thin read-only bar, value 0..1.
class ProgressBar : public Widget {
public:
    ProgressBar() { height(8); }
    float value() const { return m_value; }
    ProgressBar& value(float v);
    void onDraw(SkCanvas* canvas) override;

private:
    float m_value = 0.0f;
};

} // namespace Terracotta2
