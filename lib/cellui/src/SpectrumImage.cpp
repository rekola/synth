#include <cellui/SpectrumImage.h>

#include <canvas/Context.h>
#include <canvas/ContextCairo.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cellui {

namespace spectrum_image {

namespace {

canvas::Color toCanvas(const Color & c, float alpha = 1.0f) {
  return canvas::Color(static_cast<float>(c.getRed()) / 255.0f, static_cast<float>(c.getGreen()) / 255.0f, static_cast<float>(c.getBlue()) / 255.0f, alpha);
}

}  // namespace

std::vector<uint32_t> render(const std::vector<float> & levels, unsigned width, unsigned height, const StyleProvider & styles,
                             float scale) {
  std::vector<uint32_t> out(static_cast<size_t>(width) * height, 0);
  if (width == 0 || height == 0 || levels.size() != width) return out;

  // Drawn in canvas's logical units at the display scale: a logical size that
  // covers the image (canvas sizes are whole logical units), cropped below.
  scale = std::max(1.0f, scale);
  auto logical = [&](unsigned px) { return static_cast<unsigned>(std::ceil(static_cast<float>(px) / scale)); };
  canvas::ContextCairo ctx(logical(width), logical(height), 4, scale);
  const float w = static_cast<float>(width) / scale, h = static_cast<float>(height) / scale;
  ctx.fillStyle = toCanvas(styles.window_bg_color);
  ctx.fillRect(0, 0, logical(width), logical(height));

  // The curve, one point per pixel column centre, a unit in from the top and
  // bottom edges.
  const float inset = std::min(1.0f, h / 4.0f);
  auto x_at = [&](unsigned x) { return (static_cast<double>(x) + 0.5) / scale; };
  auto y_at = [&](unsigned x) { return h - (levels[x] * (h - 2.0f * inset) + inset); };
  ctx.beginPath().moveTo(0.0, h);
  for (unsigned x = 0; x < width; x++) ctx.lineTo(x_at(x), y_at(x));
  ctx.lineTo(w, h).closePath();

  auto fill = ctx.createLinearGradient(0, h, 0, 0);
  fill.addColorStop(0.0f, toCanvas(styles.spectrumColor(0.0f), 0.22f));
  fill.addColorStop(1.0f, toCanvas(styles.spectrumColor(1.0f), 0.22f));
  ctx.fillStyle = fill;
  ctx.fill();

  ctx.beginPath().moveTo(x_at(0), y_at(0));
  for (unsigned x = 1; x < width; x++) ctx.lineTo(x_at(x), y_at(x));
  auto stroke = ctx.createLinearGradient(0, h, 0, 0);
  stroke.addColorStop(0.0f, toCanvas(styles.spectrumColor(0.0f)));
  stroke.addColorStop(1.0f, toCanvas(styles.spectrumColor(1.0f)));
  ctx.strokeStyle = stroke;
  ctx.lineWidth = 1.5f;
  ctx.stroke();

  // The surface holds premultiplied B, G, R, A; everything is opaque, so
  // swapping the first and third byte gives straight R, G, B, A.
  auto image = ctx.getDefaultSurface().createImage(scale);
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

}  // namespace spectrum_image

}  // namespace cellui
