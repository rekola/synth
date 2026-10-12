#pragma once

#include <cellui/Theme.h>
#include <cellui/UIPlane.h>

namespace cellui {

// Draws the gallery page onto `plane`: every text style, the theme's colour
// tokens, the sub-cell glyph tables, the level meter ramp, a Markdown
// document, wide and combining characters, and a choice list, as the
// terminal shows them. Returns the number of rows used. It draws through
// UIPlane only, so any backend can show it.
int drawGallery(UIPlane & plane, const Theme & theme);

}  // namespace cellui
