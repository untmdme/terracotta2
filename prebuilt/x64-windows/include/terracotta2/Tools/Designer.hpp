#pragma once

namespace Terracotta2 {

class Widget;
class Window;

// Opens the designer as a sidebar docked to the right of `host` (default: the main window), or
// brings it to front if open. Its tab can be dragged out into its own window and back onto the
// host's right edge; it keeps its own look whatever the theme it edits. The first open moves the
// host's UI into the central node of a DockSpace (a window that already has one is used as is).
//  - Inspector: the widget tree of the selection's window above the selection's properties, with a
//    draggable split. Pick any widget in any window or click its tree row (hover previews; Esc
//    cancels or deselects), then edit its layout and type-specific properties live; "Copy C++"
//    copies its style as a setter chain.
//  - Theme: every theme() value as a live editor, with a filter; changes apply to all windows.
// Returns the designer panel (the tab's content).
Widget* openDesigner(Window* host = nullptr);
void closeDesigner(); // no-op if not open

} // namespace Terracotta2
