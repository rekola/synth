#pragma once

namespace cellui {

// The face text is drawn in: the cells' own fixed-advance one, or the
// display's proportional one, which only a display that lays text out itself
// has (any other draws the cells' own).
enum class TextFont { MONOSPACE, PROPORTIONAL };

// What becomes of text that does not fit its box: cut off, or cut off with an
// ellipsis so it is clear that it goes on.
enum class TextOverflow { CLIP, ELLIPSIS };

// How Plane::putText() sets text. Colours, weight, slant and underline are
// the plane's current ones, as for putstr().
struct TextStyle {
  TextFont font = TextFont::PROPORTIONAL;
  TextOverflow overflow = TextOverflow::CLIP;
};

}  // namespace cellui
