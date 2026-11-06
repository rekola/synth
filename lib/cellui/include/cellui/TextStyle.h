#pragma once

namespace cellui {

// The face text is drawn in: the cells' own fixed-advance one, or the
// display's proportional one, which only a display that lays text out itself
// has (any other draws the cells' own).
enum class TextFont { MONOSPACE, PROPORTIONAL };

// What becomes of text that does not fit its box: cut off, or cut off with an
// ellipsis so it is clear that it goes on.
enum class TextOverflow { CLIP, ELLIPSIS };

enum class TextSlant { UPRIGHT, ITALIC };

// The weight and slant text is written in: 100 (thin) to 900 (black), 400
// normal and 700 bold. A display with only bold and normal draws 600 and up
// bold.
constexpr int kWeightNormal = 400, kWeightBold = 700;
struct TextFace {
  int weight = kWeightNormal;
  TextSlant slant = TextSlant::UPRIGHT;
};
constexpr TextFace kPlainFace{};
constexpr TextFace kBoldFace{ kWeightBold, TextSlant::UPRIGHT };
constexpr TextFace kItalicFace{ kWeightNormal, TextSlant::ITALIC };

// How Plane::putText() sets text. Colours and underline are the plane's
// current ones, as for putstr(); the face is per call.
struct TextStyle {
  TextFont font = TextFont::PROPORTIONAL;
  TextOverflow overflow = TextOverflow::CLIP;
  TextFace face;
};

}  // namespace cellui
