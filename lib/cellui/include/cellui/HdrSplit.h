#pragma once

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
  // R, G, B, A bytes (straight alpha), the light past white in linear sRGB: the colour is each channel's
  // excess over 1 scaled so the strongest is full (the light's hue, sRGB encoded), the alpha how much of
  // it there is, the strongest excess as a share of kHdrGlowRange, sRGB encoded. A pixel with none is
  // transparent. Empty when nothing in the picture is past white.
  std::vector<uint32_t> glow;
};

// An excess of this much over white (a channel of 1 + kHdrGlowRange) is the strongest the glow layer holds.
constexpr float kHdrGlowRange = 4.0f;

// `rgba` is width*height*4 floats, row 0 at the top. One pass, with no blur: a pixel the same as the
// one before it costs a comparison, which is most of a scope.
HdrLayers splitHdr(const std::vector<float> & rgba, int width, int height);

}  // namespace cellui
