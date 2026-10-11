#include "terracotta2/Theme/Theme.hpp"
#include "terracotta2/Core/Application.hpp"
#include "terracotta2/Core/Window.hpp"

namespace Terracotta2 {

Theme darkTheme() { return Theme{}; }

Theme& appTheme() {
    static Theme t = darkTheme();
    return t;
}

static Theme* s_scoped = nullptr;
Theme& theme() { return s_scoped ? *s_scoped : appTheme(); }
ThemeScope::ThemeScope(Theme* t) : m_prev(s_scoped) { s_scoped = t; }
ThemeScope::~ThemeScope() { s_scoped = m_prev; }

static void themeChanged(Widget* w) {
    w->onThemeChanged();
    for (auto& c : w->children()) themeChanged(c.get());
}

void applyTheme() {
    for (Window* win : Application::get().windows()) {
        ThemeScope scope(win->theme()); // windows with their own theme keep it, but still refresh
        themeChanged(win->getRootWidget());
        win->getRootWidget()->markLayoutDirty();
        win->addDamage(SkRect::MakeSize(win->size()));
    }
}

} // namespace Terracotta2
