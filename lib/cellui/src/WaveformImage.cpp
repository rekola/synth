#include <cellui/WaveformImage.h>

#include <canvas/Context.h>
#include "NativeContext.h"

#include <algorithm>
#include <cmath>

namespace cellui {

namespace waveform_image {

namespace {

canvas::Color toCanvas(const Color & c) {
  auto rgb = c.toSrgb8();
  return canvas::Color(static_cast<float>(rgb[0]) / 255.0f, static_cast<float>(rgb[1]) / 255.0f, static_cast<float>(rgb[2]) / 255.0f, 1.0f);
}

}  // namespace

std::vector<uint32_t> render(const std::vector<float> & slices, const Color & fg, const Color & bg, const Color & guide, unsigned width,
                             unsigned height, float scale) {
  std::vector<uint32_t> out(static_cast<size_t>(width) * height, 0);
  if (width == 0 || height == 0) return out;

  auto context = cellui::nativeContext(width, height);
  canvas::Context & ctx = *context;
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  ctx.fillStyle = toCanvas(bg);
  ctx.fillRect(0, 0, w, h);

  // The centre line is zero amplitude; the others sit where a half-amplitude
  // bar's edges are, fainter and tinted with the clip's colour.
  const float line = std::max(1.0f, std::round(scale));
  const Color quarter = bg.blend(0.5f, guide.blend(0.5f, fg));
  ctx.fillStyle = toCanvas(quarter);
  ctx.fillRect(std::floor(w * 0.25f), 0, line, h);
  ctx.fillRect(std::floor(w * 0.75f), 0, line, h);
  ctx.fillStyle = toCanvas(guide);
  ctx.fillRect(std::floor((w - line) / 2.0f), 0, line, h);

  // Each slice is a band of the height, its bar centred across the width.
  ctx.fillStyle = toCanvas(fg);
  const size_t count = std::max<size_t>(1, slices.size());
  for (size_t i = 0; i < slices.size(); i++) {
    float level = std::clamp(slices[i], 0.0f, 1.0f);
    if (level <= 0.0f) continue;
    float y0 = h * static_cast<float>(i) / static_cast<float>(count), y1 = h * static_cast<float>(i + 1) / static_cast<float>(count);
    float bar = level * w;
    ctx.fillRect((w - bar) / 2.0f, y0, bar, y1 - y0);
  }

  // The surface holds premultiplied B, G, R, A; everything is opaque, so
  // swapping the first and third byte gives straight R, G, B, A.
  auto image = ctx.getDefaultSurface().createImage(1.0f);
  const canvas::ImageData & data = image->getData();
  if (data.getNumChannels() != 4 || data.getWidth() < width || data.getHeight() < height) return out;
  const unsigned char * src = data.getData();
  const size_t stride = static_cast<size_t>(data.getWidth()) * 4;
  for (unsigned y = 0; y < height; y++) {
    for (unsigned x = 0; x < width; x++) {
      const unsigned char * p = src + y * stride + static_cast<size_t>(x) * 4;
      uint32_t b = p[0], g = p[1], r = p[2];
      out[static_cast<size_t>(y) * width + x] = r | (g << 8) | (b << 16) | (0xffu << 24);
    }
  }
  return out;
}

}  // namespace waveform_image

}  // namespace cellui
