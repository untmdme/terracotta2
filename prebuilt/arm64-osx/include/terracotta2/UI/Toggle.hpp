#pragma once
#include <functional>
#include <string>
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2 {

// Value controls below follow one pattern: value() reads, value(v) sets without firing onChange,
// onChange(fn) fires on user edits. Click or Space toggles; arrows step a focused Slider.

// On/off switch. The knob slides when toggled.
class Toggle : public Widget {
public:
    explicit Toggle(bool on = false);
    Toggle& onChange(std::function<void(bool)> fn) { m_onChange = std::move(fn); return *this; }
    bool value() const { return m_on; }
    Toggle& value(bool on);

    bool onMouseClick(const MouseEvent& e) override;
    bool onKey(const KeyEvent& e) override;
    bool isFocusable() const override { return true; }
    Cursor cursor() const override { return Cursor::Hand; }
    void onDraw(SkCanvas* canvas) override;

private:
    void flip();
    bool m_on;
    float m_t; // knob position 0..1, animated
    std::function<void(bool)> m_onChange;
};

// Box with a text label to its right; clicking the label toggles too.
class Checkbox : public Widget {
public:
    explicit Checkbox(std::string text, bool checked = false);
    Checkbox& onChange(std::function<void(bool)> fn) { m_onChange = std::move(fn); return *this; }
    bool value() const { return m_checked; }
    Checkbox& value(bool checked);

    bool onMouseClick(const MouseEvent& e) override;
    bool onKey(const KeyEvent& e) override;
    bool isFocusable() const override { return true; }
    Cursor cursor() const override { return Cursor::Hand; }
    void onMouseEnter() override { markPaintDirty(); }
    void onMouseLeave() override { markPaintDirty(); }
    void onDraw(SkCanvas* canvas) override;

private:
    void flip();
    bool m_checked;
    std::function<void(bool)> m_onChange;
};

} // namespace Terracotta2
