#pragma once
#include <string>
#include "include/core/SkColor.h"

namespace Terracotta2 {

struct LabelStyle {
    SkColor text = SkColorSetRGB(220, 220, 220);
    float fontSize = 13.0f;
};

struct ButtonStyle {
    SkColor background = SkColorSetRGB(50, 50, 50);
    SkColor hoverBackground = SkColorSetRGB(70, 70, 70);
    SkColor activeBackground = SkColorSetRGB(30, 90, 180);
    SkColor text = SkColorSetRGB(230, 230, 230);
    float cornerRadius = 4.0f;
    float paddingX = 10.0f;
    float paddingY = 5.0f;
    float hoverFadeSec = 0.12f;
};

struct InputStyle {
    SkColor background = SkColorSetRGB(25, 25, 25);
    SkColor border = SkColorSetRGB(60, 60, 60);
    SkColor focusBorder = SkColorSetRGB(30, 90, 180);
    SkColor text = SkColorSetRGB(240, 240, 240);
    SkColor placeholder = SkColorSetRGB(120, 120, 120);
    SkColor caret = SkColorSetRGB(240, 240, 240);
    float cornerRadius = 4.0f;
    float paddingX = 6.0f;
    float height = 24.0f;
};

struct DockStyle {
    SkColor tabBarBackground = SkColorSetRGB(24, 24, 24);
    SkColor tabActive = SkColorSetRGB(40, 40, 40);
    SkColor tabInactive = SkColorSetRGB(30, 30, 30);
    SkColor tabHover = SkColorSetRGB(48, 48, 48);
    SkColor tabText = SkColorSetRGB(230, 230, 230);
    SkColor tabTextInactive = SkColorSetRGB(150, 150, 150);
    SkColor splitter = SkColorSetRGB(15, 15, 15);
    SkColor splitterHover = SkColorSetRGB(30, 90, 180);
    SkColor dropPreview = SkColorSetARGB(70, 30, 90, 180);
    float tabHeight = 26.0f;
    float tabPaddingX = 12.0f;
    float splitterSize = 4.0f;
};

struct ScrollStyle {
    SkColor thumb = SkColorSetARGB(110, 160, 160, 160);
    SkColor thumbActive = SkColorSetARGB(180, 190, 190, 190);
    float thickness = 6.0f;
    float margin = 2.0f;
    float wheelSpeed = 1.0f; // multiplier on platform scroll deltas
};

struct PopupStyle { // menus, dropdown lists, pickers (Overlay / PopupBox)
    SkColor background = SkColorSetRGB(42, 42, 46);
    SkColor border = SkColorSetRGB(70, 70, 76);
    SkColor itemHover = SkColorSetARGB(160, 70, 110, 200);
    float cornerRadius = 6.0f;
    float itemHeight = 24.0f;
};

struct TooltipStyle {
    SkColor background = SkColorSetRGB(58, 58, 64);
    SkColor border = SkColorSetRGB(84, 84, 92);
    SkColor text = SkColorSetRGB(232, 232, 236);
    float fontSize = 12.0f;
    float delaySec = 0.5f; // pointer at rest this long before it shows
};

struct Theme {
    SkColor globalBackground = SkColorSetRGB(20, 20, 20);
    SkColor panelBackground = SkColorSetRGB(35, 35, 35);
    SkColor accent = SkColorSetRGB(70, 130, 230);          // toggles, sliders, progress, focus
    SkColor track = SkColorSetRGB(58, 58, 62);             // inactive part of sliders/toggles/bars
    SkColor textMuted = SkColorSetRGB(140, 140, 146);      // captions, hints
    SkColor selection = SkColorSetARGB(110, 70, 130, 230); // selected-text highlight
    std::string fontFamily = "Helvetica Neue"; // system family, or one returned by loadFont()
    LabelStyle label;
    ButtonStyle button;
    InputStyle input;
    DockStyle dock;
    ScrollStyle scroll;
    PopupStyle popup;
    TooltipStyle tooltip;
};

// The app theme (mutable). Defaults to darkTheme(). After changing it at runtime call
// applyTheme(): every widget in every window re-reads it (relayout + full repaint).
Theme& appTheme();
void applyTheme();

// The theme in effect: the app theme, except while a window with its own theme
// (Window::setTheme) lays out, paints or handles input. Widgets read this one.
Theme& theme();

// While alive, theme() returns `t` (nullptr: the app theme). Use it when building widgets
// for a window that has its own theme outside its event handlers.
class ThemeScope {
public:
    explicit ThemeScope(Theme* t);
    ~ThemeScope();
    ThemeScope(const ThemeScope&) = delete;
    ThemeScope& operator=(const ThemeScope&) = delete;

private:
    Theme* m_prev;
};
Theme darkTheme();

} // namespace Terracotta2
