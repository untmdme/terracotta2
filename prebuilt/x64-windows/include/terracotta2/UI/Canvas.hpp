#pragma once
#include <functional>
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2 {

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

} // namespace Terracotta2
