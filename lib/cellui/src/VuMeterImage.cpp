#include <cellui/VuMeterImage.h>

#include <canvas/Context.h>
#include <canvas/ContextCairo.h>

#include <algorithm>
#include <cmath>

namespace cellui {

namespace vu_meter_image {

namespace {

canvas::Color toCanvas(const Color & c) {
  return canvas::Color(static_cast<float>(c.getRed()) / 255.0f, static_cast<float>(c.getGreen()) / 255.0f, static_cast<float>(c.getBlue()) / 255.0f, 1.0f);
}

}  // namespace

std::vector<uint32_t> render(const std::vector<std::optional<VuReading>> & columns, const VuMeterLook & look, unsigned width,
                             unsigned height, const StyleProvider & styles, float scale) {
  std::vector<uint32_t> out(static_cast<size_t>(width) * height, 0);
  if (width == 0 || height == 0) return out;

  canvas::ContextCairo ctx(width, height);
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  ctx.fillStyle = toCanvas(look.background);
  ctx.fillRect(0, 0, w, h);
  const float thickness = std::max(1.0f, std::round(scale));
  const float slot = columns.empty() ? w : w / static_cast<float>(columns.size());

  for (size_t i = 0; i < columns.size(); i++) {
    if (!columns[i]) continue;
    const VuReading & reading = *columns[i];
    // The bar in the middle of its slot, a gap either side.
    const float x0 = std::round(slot * (static_cast<float>(i) + 0.15f));
    const float x1 = std::max(x0 + 1.0f, std::round(slot * (static_cast<float>(i) + 0.85f)));
    const float fill = std::clamp(reading.fraction, 0.0f, 1.0f) * h;
    if (fill > 0.0f) {
      if (reading.clipping) {
        ctx.fillStyle = toCanvas(styles.meter_clip_color);
      } else if (look.color) {
        ctx.fillStyle = toCanvas(*look.color);
      } else {
        // The meter colours by height, over the whole meter.
        auto gradient = ctx.createLinearGradient(0, h, 0, 0);
        for (int k = 0; k <= 8; k++) {
          float t = static_cast<float>(k) / 8.0f;
          gradient.addColorStop(t, toCanvas(styles.meterColor(t)));
        }
        ctx.fillStyle = gradient;
      }
      ctx.fillRect(x0, h - fill, x1 - x0, fill);
    }
    // The peak: a line as thick as the display scale, at the peak's height.
    if (reading.peak > 0.0f) {
      float y = std::clamp(h - std::clamp(reading.peak, 0.0f, 1.0f) * h, 0.0f, h - thickness);
      Color peak_color = reading.clipping ? styles.meter_clip_color : look.color ? *look.color : styles.meterColor(reading.peak);
      ctx.fillStyle = toCanvas(peak_color);
      ctx.fillRect(x0, y, x1 - x0, thickness);
    }
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

}  // namespace vu_meter_image

}  // namespace cellui
