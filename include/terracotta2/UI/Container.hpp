#pragma once
#include <functional>
#include <optional>
#include "include/core/SkPaint.h"
#include "terracotta2/Theme/Theme.hpp"
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2 {

class VBox : public Widget {
public:
    VBox() { direction(YGFlexDirectionColumn); }
};

class HBox : public Widget {
public:
    HBox() { direction(YGFlexDirectionRow); }
};

class Panel : public Widget {
    std::optional<SkColor> m_bgColor; // none: theme().panelBackground

public:
    explicit Panel(std::optional<SkColor> bgColor = std::nullopt) : m_bgColor(bgColor) {
        direction(YGFlexDirectionColumn); // sized by content like any widget; add .grow(1) to fill
    }
    Panel& background(SkColor c) { m_bgColor = c; markPaintDirty(); return *this; }
    SkColor background() const { return m_bgColor.value_or(theme().panelBackground); }
    void onDraw(SkCanvas* canvas) override {
        SkPaint paint;
        paint.setColor(m_bgColor.value_or(theme().panelBackground));
        canvas->drawRect(m_bounds, paint);
    }
};

// Custom drawing without subclassing: fn(canvas, bounds) runs whenever this area repaints.
// Call markPaintDirty() when what you draw changes. For input or state, subclass Widget
// and override onDraw / onMouseDown / ... instead.
class Canvas : public Widget {
public:
    using DrawFn = std::function<void(SkCanvas*, const SkRect& bounds)>;
    explicit Canvas(DrawFn fn) : m_draw(std::move(fn)) {}
    void onDraw(SkCanvas* canvas) override { if (m_draw) m_draw(canvas, m_bounds); }

private:
    DrawFn m_draw;
};

// Vertical scroller. Add children directly; they keep their natural height
// (Yoga overflow: scroll) and are offset by the scroll position.
// Wheel/trackpad scrolls; the scrollbar thumb (drawn over content) can be dragged.
class ScrollView : public Widget {
public:
    ScrollView();

    float scrollY() const { return m_scrollY; }
    void setScrollY(float y);         // clamped to [0, maxScroll()]
    float contentHeight() const;      // bottom of the lowest child, relative to the view
    float maxScroll() const;

    bool onScroll(float dx, float dy) override;
    bool onMouseDown(const MouseEvent& e) override; // thumb drag
    bool onMouseMove(const MouseEvent& e) override;
    bool onMouseUp(const MouseEvent& e) override;
    void onDrawOver(SkCanvas* canvas) override;     // scrollbar
    void onLayout() override;                       // re-clamp after resize
    Widget* hitTest(float x, float y) override;     // thumb wins over content beneath it

protected:
    SkPoint childOffset() const override { return {0, -m_scrollY}; }

private:
    SkRect thumbRect() const;
    float m_scrollY = 0.0f;
    float m_dragGrabY = -1.0f; // >= 0 while dragging the thumb
};

} // namespace Terracotta2
