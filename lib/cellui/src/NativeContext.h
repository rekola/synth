#ifndef CELLUI_NATIVECONTEXT_H
#define CELLUI_NATIVECONTEXT_H

#include <cellui/Picture.h>

#include <canvas/Context.h>

#include <memory>

namespace cellui {

// A drawing context on the platform's own canvas backend: Cairo, Quartz 2D on
// Apple platforms or GDI+ on Windows.
std::unique_ptr<canvas::Context> nativeContext(unsigned width, unsigned height, unsigned channels = 4, float display_scale = 1.0f);

// What a picture is drawn on: a context of exactly `width` x `height` pixels whose colours are
// linear Rec.2020 light (1.0 = SDR white, HDR above it; canvas::ColorSpace::LINEAR_REC2020).
// `prefer_float` asks for a float surface, which keeps every colour as given; a backend with none
// gives an 8-bit one, where canvas itself maps each colour to sRGB, so the caller draws the same
// either way and only reads the result by `is_float`.
struct LinearContext {
  std::unique_ptr<canvas::Context> context;
  bool is_float = false;
};
LinearContext nativeLinearContext(unsigned width, unsigned height, float display_scale, bool prefer_float);

// The top-left width x height pixels of what was drawn, as a Picture: floats for a float context, else
// 8-bit R, G, B, A. Empty (no pixels at all) when the surface is smaller or in a format not expected.
Picture readPicture(LinearContext & drawn, unsigned width, unsigned height, float display_scale);

}  // namespace cellui

#endif
