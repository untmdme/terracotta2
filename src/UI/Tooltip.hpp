#pragma once
// Internal: the tooltip layer. Window drives it from its pointer and key events.
#include "include/core/SkPoint.h"

namespace Terracotta2 {
class Widget;
namespace tooltip {

void hover(Widget* root, Widget* hovered, SkPoint at); // pointer moved over `hovered` (may be null)
void hide(Widget* root, bool forget); // press/scroll/key: hide; forget = pointer left, show again on return

} // namespace tooltip
} // namespace Terracotta2
