#pragma once
#include <functional>
#include <string>
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2 {

class Label;

class Button : public Widget {
public:
    explicit Button(std::string text);
    Button& onClick(std::function<void()> fn) { m_onClick = std::move(fn); return *this; }

    bool onMouseDown(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    bool onMouseClick(const MouseEvent& e) override;
    void onMouseEnter() override;
    void onMouseLeave() override;
    Cursor cursor() const override { return Cursor::Hand; }
    bool isFocusable() const override { return true; }
    bool onKey(const KeyEvent& e) override; // Space / Enter click
    void onThemeChanged() override;
    void onDraw(SkCanvas* canvas) override;

private:
    std::function<void()> m_onClick;
    Label* m_label;
    float m_hover = 0.0f; // 0..1, animated
    bool m_pressed = false;
};

} // namespace Terracotta2
