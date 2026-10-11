#pragma once

namespace Terracotta2 {

class Window;

// Opens the designer window (or raises it if open): every theme() value as a live editor,
// plus an inspector to pick any widget in another window and edit its layout. Changes apply
// to all windows immediately (applyTheme()).
Window* openDesigner();

} // namespace Terracotta2
