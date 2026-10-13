#pragma once
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2 {

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
    void scrollToShow(const SkRect& r); // minimal scroll that brings r (a descendant's bounds) into view
    // scrollToShow(r) on every ScrollView around `w` (r in w's coordinates, e.g. its bounds).
    static void reveal(Widget& w, const SkRect& r);
    // The scroll position thumb (theme().scroll) along `track`'s right edge for `content` px of
    // content seen through `view` px, scrolled by `scroll`; empty when everything fits.
    static SkRect thumbRect(const SkRect& track, float scroll, float content, float view);
    static void drawThumb(SkCanvas* canvas, const SkRect& thumb, bool active = false);

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
