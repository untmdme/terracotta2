#pragma once
#include <optional>
#include "include/core/SkPaint.h"
#include "terracotta2/Theme/Theme.hpp"
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2 {

class VBox : public Widget {
public:
    VBox() { direction(Direction::Column); }
};

class HBox : public Widget {
public:
    HBox() { direction(Direction::Row); }
};

class Panel : public Widget {
    std::optional<SkColor> m_bgColor; // none: theme().panelBackground

public:
    explicit Panel(std::optional<SkColor> bgColor = std::nullopt) : m_bgColor(bgColor) {
        direction(Direction::Column); // sized by content like any widget; add .grow(1) to fill
    }
    Panel& background(SkColor c) { m_bgColor = c; markPaintDirty(); return *this; }
    SkColor background() const { return m_bgColor.value_or(theme().panelBackground); }
    void onDraw(SkCanvas* canvas) override {
        SkPaint paint;
        paint.setColor(m_bgColor.value_or(theme().panelBackground));
        canvas->drawRect(m_bounds, paint);
    }
};

} // namespace Terracotta2
