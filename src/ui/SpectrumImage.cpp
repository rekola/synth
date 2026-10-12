#include "SpectrumImage.h"

#include <canvas/Context.h>
#include <canvas/ContextCairo.h>

#include <algorithm>
#include <cstring>

namespace spectrum_image {

namespace {

canvas::Color toCanvas(const Color & c, float alpha = 1.0f) {
  return canvas::Color(static_cast<float>(c.getRed()) / 255.0f, static_cast<float>(c.getGreen()) / 255.0f, static_cast<float>(c.getBlue()) / 255.0f, alpha);
}

}  // namespace

std::vector<uint32_t> render(const std::vector<float> & levels, unsigned width, unsigned height, const StyleProvider & styles) {
  std::vector<uint32_t> out(static_cast<size_t>(width) * height, 0);
  if (width == 0 || height == 0 || levels.size() != width) return out;

  canvas::ContextCairo ctx(width, height);
  const float h = static_cast<float>(height);
  ctx.fillStyle = toCanvas(styles.window_bg_color);
  ctx.fillRect(0, 0, width, height);

  // The curve, one point per column centre, a pixel in from the top and
  // bottom edges.
  auto y_at = [&](unsigned x) { return h - (levels[x] * (h - 2.0f) + 1.0f); };
  ctx.beginPath().moveTo(0.0, h);
  for (unsigned x = 0; x < width; x++) ctx.lineTo(static_cast<double>(x) + 0.5, y_at(x));
  ctx.lineTo(static_cast<double>(width), h).closePath();

  auto fill = ctx.createLinearGradient(0, h, 0, 0);
  fill.addColorStop(0.0f, toCanvas(styles.spectrumColor(0.0f), 0.22f));
  fill.addColorStop(1.0f, toCanvas(styles.spectrumColor(1.0f), 0.22f));
  ctx.fillStyle = fill;
  ctx.fill();

  ctx.beginPath().moveTo(0.5, y_at(0));
  for (unsigned x = 1; x < width; x++) ctx.lineTo(static_cast<double>(x) + 0.5, y_at(x));
  auto stroke = ctx.createLinearGradient(0, h, 0, 0);
  stroke.addColorStop(0.0f, toCanvas(styles.spectrumColor(0.0f)));
  stroke.addColorStop(1.0f, toCanvas(styles.spectrumColor(1.0f)));
  ctx.strokeStyle = stroke;
  ctx.lineWidth = 1.5f;
  ctx.stroke();

  // The surface holds premultiplied B, G, R, A; everything is opaque, so
  // swapping the first and third byte gives straight R, G, B, A.
  auto image = ctx.getDefaultSurface().createImage(1.0f);
  const canvas::ImageData & data = image->getData();
  if (data.getNumChannels() != 4 || data.getWidth() != width || data.getHeight() != height) return out;
  const unsigned char * src = data.getData();
  for (size_t i = 0; i < out.size(); i++) {
    uint32_t b = src[4 * i], g = src[4 * i + 1], r = src[4 * i + 2];
    out[i] = r | (g << 8) | (b << 16) | (0xffu << 24);
  }
  return out;
}

}  // namespace spectrum_image
