#pragma once

#include <cellui/Color.h>

#include <cstdint>
#include <vector>

namespace cellui {

// The two layers a display with sRGB primaries and a finite white shows a linear Rec.2020 float picture
// (Image::hdr) as: the picture itself, gamut mapped and compressed to what it can show, and the light
// that did not fit, which the display spreads around as bloom.
struct HdrLayers {
  // R, G, B, A bytes, width*height of them: the picture as Color::toSrgb8() maps each colour (the gamut
  // mapped in, light past white scaled to fit and washed toward white).
  std::vector<uint32_t> base;
  // R, G, B, A bytes (straight alpha), the light past white (see glowOf(): the colour's own hue at full
  // strength, the alpha how much of it there is). A pixel with none is transparent. Empty when nothing in the
  // picture is past white.
  std::vector<uint32_t> glow;
};

// An excess of this much over white (a channel of 1 + kHdrGlowRange) is the strongest the glow layer holds.
constexpr float kHdrGlowRange = 4.0f;

// The glow of one colour, as R, G, B, A bytes (straight alpha): the colour's own hue (its linear sRGB
// channels scaled so the strongest is full, sRGB encoded), which is what the light past white of it
// is the colour of, and as the alpha how much of it there is, the strongest channel's excess over white as a
// share of kHdrGlowRange, sRGB encoded. Image::pack(0, 0, 0, 0) for a colour at or under white. The window
// blooms a picture's pixels and a cell's text from this, so the same colour glows the same in both.
uint32_t glowOf(const Color & color);

// `rgba` is width*height*4 floats, row 0 at the top. One pass, with no blur: a pixel the same as the
// one before it costs a comparison, which is most of a scope.
HdrLayers splitHdr(const std::vector<float> & rgba, int width, int height);

}  // namespace cellui
