#pragma once

#include <cellui/Color.h>

#include <cstdint>
#include <vector>

namespace cellui {

namespace waveform_image {

// One pattern row's slice of a waveform as a width x height image: the
// amplitudes (0..1) of consecutive time slices, top to bottom, each a bar
// centred across the width, over guide lines at the centre and the half-
// amplitude positions. Returns width*height pixels, row 0 at the top, bytes
// R, G, B, A (all opaque).
std::vector<uint32_t> render(const std::vector<float> & slices, const Color & fg, const Color & bg, const Color & guide, unsigned width,
                             unsigned height, float scale = 1.0f);

}  // namespace waveform_image

}  // namespace cellui
