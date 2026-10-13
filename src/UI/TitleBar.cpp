#include "terracotta2/UI/TitleBar.hpp"
#include <cmath>
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPaint.h"
#include "Text.hpp"
#include "terracotta2/Core/Window.hpp"
#include "terracotta2/Theme/Theme.hpp"

namespace Terracotta2 {

namespace {
#ifdef __APPLE__
constexpr float kBarHeight = 28; // the native titlebar the traffic lights are centered in
#else
constexpr float kBarHeight = 32;
#endif

// The icon font Windows draws its own caption buttons with: Segoe Fluent Icons on Windows 11,
// Segoe MDL2 Assets on Windows 10.
sk_sp<SkTypeface> captionIcons() {
    static sk_sp<SkTypeface> tf = [] () -> sk_sp<SkTypeface> {
        sk_sp<SkFontMgr> mgr = detail::platformFontMgr();
        if (!mgr) return nullptr;
        for (const char* family : {"Segoe Fluent Icons", "Segoe MDL2 Assets"})
            if (sk_sp<SkTypeface> t = mgr->matchFamilyStyle(family, SkFontStyle())) return t;
        return nullptr;
    }();
    return tf;
}

// One caption button, drawn like the Windows 11 ones: a flat 46 px cell with a 10 px icon-font glyph.
class CaptionButton : public Widget {
public:
    enum class Kind { Minimize, Maximize, Close };
    explicit CaptionButton(Kind kind) : m_kind(kind) { width(kWindowButtonsWidth / 3).alignSelf(Align::Stretch); } // as tall as the bar
    bool onMouseDown(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        m_pressed = true;
        markPaintDirty();
        return true;
    }
    bool onMouseUp(const MouseEvent&) override {
        m_pressed = false;
        markPaintDirty();
        return false;
    }
    bool onMouseClick(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        Window* w = window();
        if (m_kind == Kind::Minimize) w->minimize();
        else if (m_kind == Kind::Maximize) w->toggleMaximize();
        else w->requestClose();
        return true;
    }
    void onMouseEnter() override { markPaintDirty(); }
    void onMouseLeave() override { markPaintDirty(); }
    void onLayout() override { markPaintDirty(); } // maximized <-> restored changes the glyph
    void onDraw(SkCanvas* c) override {
        const bool hot = isHovered(), down = hot && m_pressed;
        SkColor fg = theme().label.text;
        if (!hot && window() && !window()->isActive()) fg = detail::lerpColor(fg, theme().panelBackground, 0.6f); // inactive window: 40%, like its title
        SkPaint p;
        p.setAntiAlias(true);
        if (hot) {
            if (m_kind == Kind::Close) { // Windows' close red, a little lighter while pressed
                p.setColor(down ? SkColorSetRGB(200, 64, 49) : SkColorSetRGB(196, 43, 28));
                fg = SK_ColorWHITE;
            } else { // a faint wash of the text color over the bar, weaker while pressed
                p.setColor(SkColorSetA(fg, down ? 0x0A : 0x10));
            }
            c->drawRect(m_bounds, p);
        }

        sk_sp<SkTypeface> tf = captionIcons();
        if (!tf) return;
        const bool restore = m_kind == Kind::Maximize && window() && window()->isMaximized();
        const SkUnichar cp = m_kind == Kind::Minimize ? 0xE921 : m_kind == Kind::Close ? 0xE8BB : restore ? 0xE923 : 0xE922;
        SkFont font(tf, 10);
        font.setEdging(SkFont::Edging::kAntiAlias);
        const SkGlyphID glyph = font.unicharToGlyph(cp);
        SkRect gb;
        font.getBounds({&glyph, 1}, {&gb, 1}, nullptr);
        p.setColor(fg);
        c->drawSimpleText(&glyph, sizeof(glyph), SkTextEncoding::kGlyphID, std::round(m_bounds.centerX() - gb.centerX()),
                          std::round(m_bounds.centerY() - gb.centerY()), font, p);
    }

private:
    Kind m_kind;
    bool m_pressed = false;
};
} // namespace

TitleBar::TitleBar() {
    height(kBarHeight).shrink(0).alignItems(Align::Center).gap(8);
    padding(Edge::Left, kTrafficLightsWidth > 0 ? kTrafficLightsWidth : 12).padding(Edge::Right, kTrafficLightsWidth > 0 ? 12 : 0); // Windows: buttons flush right
}

void TitleBar::onDraw(SkCanvas* c) {
    SkPaint p;
    p.setColor(theme().panelBackground);
    c->drawRect(m_bounds, p);
}

WindowButtons::WindowButtons() {
#ifndef __APPLE__
    alignSelf(Align::Stretch).margin(Edge::Left, 8);
    add<CaptionButton>(CaptionButton::Kind::Minimize);
    m_maximize = &add<CaptionButton>(CaptionButton::Kind::Maximize);
    add<CaptionButton>(CaptionButton::Kind::Close);
#endif
}

} // namespace Terracotta2
