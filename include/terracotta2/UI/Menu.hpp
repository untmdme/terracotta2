#pragma once
#include <functional>
#include <string>
#include <vector>
#include "terracotta2/UI/Box.hpp"

namespace Terracotta2 {

class Label;

// A menu of `items` at `at` (window coords); onPick(index) runs after it closes. `current` is
// highlighted. Use for context menus: openMenu(*this, mapToWindow(SkPoint{e.x, e.y}), ...).
// Keyboard: Up/Down/Home/End move, Enter or Space picks, Escape closes.
void openMenu(Widget& from, SkPoint at, const std::vector<std::string>& items, std::function<void(int)> onPick,
              int current = -1);

// Field showing the selected item; click (or Space/Enter/Down when focused) opens the list.
class Dropdown : public HBox {
public:
    explicit Dropdown(std::vector<std::string> items, int selected = 0);
    Dropdown& onChange(std::function<void(int)> fn) { m_onChange = std::move(fn); return *this; }
    int value() const { return m_selected; }
    Dropdown& value(int index); // clamped to the items

    bool onMouseDown(const MouseEvent& e) override { return e.button == MouseButton::Left; }
    bool onMouseClick(const MouseEvent& e) override;
    bool onKey(const KeyEvent& e) override;
    bool isFocusable() const override { return true; }
    Cursor cursor() const override { return Cursor::Hand; }
    void onMouseEnter() override { markPaintDirty(); }
    void onMouseLeave() override { markPaintDirty(); }
    void onThemeChanged() override;
    void onDraw(SkCanvas* canvas) override;

private:
    void openList();
    std::vector<std::string> m_items;
    int m_selected = 0;
    Label* m_label;
    std::function<void(int)> m_onChange;
};

} // namespace Terracotta2
