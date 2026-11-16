#pragma once

#include <cellui/Color.h>
#include <cellui/Picture.h>
#include <cellui/StyleProvider.h>

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

// The amplitude from which a bar is lit past white, and how much more light the loudest slice has than the
// colour it is given.
constexpr float kHdrStart = 0.85f, kHdrGain = 2.0f;

// The same as a Picture, drawn on a float canvas when `prefer_float` and the backend has one (a bar near
// full scale is lit past white), else on an 8-bit one that maps each colour to sRGB.
Picture renderPicture(const std::vector<float> & slices, const Color & fg, const Color & bg, const Color & guide, unsigned width, unsigned height,
                      float scale, bool prefer_float);

}  // namespace waveform_image

}  // namespace cellui
