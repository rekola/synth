#pragma once

#include <cellui/Color.h>

#include <optional>

namespace cellui {

// What a cell does to the layers below it: blurs them over `blur` rows (and
// twice as many columns, a cell being about twice as tall as wide) and pulls
// them `dim` of the way toward `tint`. A modal dialog's scrim is a layer of
// such cells. A display with pixels blurs pixels; the terminal blurs cells:
// a cell's colour is its background mixed with its glyph's ink, averaged
// over its neighbours, and its glyph fades into that average, gone from
// kGlyphGoneBlur on.
struct Backdrop {
  static constexpr int kMaxBlur = 4;
  static constexpr int kGlyphGoneBlur = 3;

  int blur = 0;               // radius in rows, 0 to kMaxBlur
  float dim = 0.0f;           // 0 to 1
  std::optional<Color> tint = std::nullopt;  // unset: the display's own background

  bool active() const { return blur > 0 || dim > 0.0f; }
  bool operator==(const Backdrop & o) const {
    auto same = [](const Color & a, const Color & b) {
      return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
    };
    return blur == o.blur && dim == o.dim && tint.has_value() == o.tint.has_value() && (!tint || same(*tint, *o.tint));
  }
  bool operator!=(const Backdrop & o) const { return !(*this == o); }
};

}  // namespace cellui
