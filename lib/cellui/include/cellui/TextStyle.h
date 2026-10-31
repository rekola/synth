#pragma once

namespace cellui {

// The face text is drawn in: the cells' own fixed-advance one, or the
// display's proportional one, which only a display that lays text out itself
// has (any other draws the cells' own).
enum class TextFont { MONOSPACE, PROPORTIONAL };

// What becomes of text that does not fit its box: cut off, or cut off with an
// ellipsis so it is clear that it goes on.
enum class TextOverflow { CLIP, ELLIPSIS };

// The slant of a region of text; INHERIT keeps the plane's current one.
enum class TextSlant { INHERIT, UPRIGHT, ITALIC };

// How Plane::putText() sets text. Colours and underline are the plane's
// current ones, as for putstr(); weight and slant are the plane's unless
// given here, per call.
struct TextStyle {
  TextFont font = TextFont::PROPORTIONAL;
  TextOverflow overflow = TextOverflow::CLIP;
  // 100 (thin) to 900 (black), 400 normal and 700 bold; 0 keeps the plane's
  // (bold or normal). A display with only bold and normal draws 600 and up
  // bold.
  int weight = 0;
  TextSlant slant = TextSlant::INHERIT;
};

constexpr int kWeightNormal = 400, kWeightBold = 700;

// The weight a text is drawn at, given the plane's own bold state.
constexpr int resolveWeight(const TextStyle & style, bool plane_bold) {
  if (style.weight <= 0) return plane_bold ? kWeightBold : kWeightNormal;
  return style.weight > 900 ? 900 : (style.weight < 100 ? 100 : style.weight);
}

constexpr bool resolveItalic(const TextStyle & style, bool plane_italic) {
  return style.slant == TextSlant::INHERIT ? plane_italic : style.slant == TextSlant::ITALIC;
}

}  // namespace cellui
