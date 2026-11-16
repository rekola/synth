#pragma once

#include <cellui/Image.h>

namespace cellui {

// The strengths of the light past white in a picture, as the window shows it: the halo's blur and the
// core's, in cell heights, the gain the glow's alpha is multiplied by after the blur, how much of each
// is added, and what is taken off the core's alpha so a weak glow makes no core.
constexpr double kPictureHaloRadius = 0.5, kPictureHaloGain = 12.0, kPictureHaloStrength = 0.8;
constexpr double kPictureCoreRadius = 0.1, kPictureCoreGain = 10.0, kPictureCoreThreshold = 0.5, kPictureCoreStrength = 1.0;

// What a display with no GPU shows of a float picture (Image::hdr): the picture tone mapped as
// splitHdr() does, with its light past white blurred on the CPU, wide as a halo and a little as a core
// that thickens what is brightest, and screened over it, in the light's own colour. The result is an
// 8-bit picture (`pixels`, same size, version, stretch and pad) and the glow stays inside it, since a
// terminal's picture cannot spill past its cells. `cell_h` is the cell's height in pixels, which the
// blur radii are in. A picture with nothing past white is only tone mapped; an 8-bit one is returned as it is.
//
// With `state`, a picture the same size as the last one given is not done again: only the rectangle
// whose floats changed, grown by how far the blur reaches, is recomputed, and every other pixel is
// the last result's, so what a terminal diffs (Sixel's cells) stays equal outside the damage.
struct HdrFlattenState {
  std::vector<float> source;  // the floats the result was made from
  Image result;
  int cell_h = 0;
};
Image flattenHdr(const Image & image, int cell_h, HdrFlattenState * state = nullptr);

// How far, in pixels, the glow of one pixel reaches at this cell height.
int hdrBloomReach(int cell_h);

}  // namespace cellui
