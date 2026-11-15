#include <cellui/StageStats.h>
#include <cellui/SparklineImage.h>

#include <canvas/Context.h>
#include "NativeContext.h"

#include <algorithm>
#include <cmath>

namespace cellui {

namespace sparkline_image {

namespace {

// A colour as canvas takes it from a linear Rec.2020 context: the components as they are.
canvas::Color toCanvas(const Color & c) { return canvas::Color(c.r(), c.g(), c.b(), 1.0f); }

}  // namespace

Picture renderPicture(const std::vector<std::optional<SparklineReading>> & columns, const SparklineLook & look, unsigned width,
                      unsigned height, const StyleProvider & styles, float scale, bool prefer_float) {
  StageTimer timer(Stage::PICTURE_RENDER);
  Picture blank;
  blank.width = static_cast<int>(width);
  blank.height = static_cast<int>(height);
  blank.pixels.assign(static_cast<size_t>(width) * height, 0);
  if (width == 0 || height == 0) return blank;

  LinearContext drawn = nativeLinearContext(width, height, 1.0f, prefer_float);
  if (!drawn.context) return blank;
  canvas::Context & ctx = *drawn.context;
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  ctx.fillStyle = toCanvas(look.background);
  ctx.fillRect(0, 0, w, h);
  const float thickness = std::max(1.0f, std::round(scale));
  // Every slot is the same whole number of pixels, so the bars are evenly
  // spaced; what is left over is split between the left and right edges.
  const unsigned count = static_cast<unsigned>(std::max<size_t>(1, columns.size()));
  const unsigned slot = std::max(1u, width / count);
  const unsigned margin = width > slot * count ? (width - slot * count) / 2 : 0;
  const unsigned bar = std::clamp(static_cast<unsigned>(std::lround(static_cast<float>(slot) * 0.7f)), 1u, slot);
  const unsigned gap = (slot - bar) / 2;
  // The clip lamp is a square as wide as a bar at the top of the picture, with a gap under it; the bar's scale is
  // what is left. A picture too short to have both has no lamp (a clip is then not shown).
  const unsigned lamp = std::max(3u, bar);
  const bool has_lamp = height >= 2 * (lamp + 1);
  const float zone = has_lamp ? static_cast<float>(lamp + 1) : 0.0f;
  const float span = h - zone;  // the bar's scale: from the bottom up to here

  for (size_t i = 0; i < columns.size(); i++) {
    if (!columns[i]) continue;
    const SparklineReading & reading = *columns[i];
    // The bar in the middle of its slot, a gap either side.
    const float x0 = static_cast<float>(margin + static_cast<unsigned>(i) * slot + gap);
    const float x1 = x0 + static_cast<float>(bar);
    if (has_lamp) {
      ctx.fillStyle = toCanvas(styles.clipLedColor(reading.saturated, look.background));
      ctx.fillRect(x0, 0, x1 - x0, static_cast<float>(lamp));
    }
    const float fill = std::clamp(reading.fraction, 0.0f, 1.0f) * span;
    if (fill > 0.0f) {
      if (look.color) {
        ctx.fillStyle = toCanvas(*look.color);
      } else {
        // The meter colours by height, over the whole scale: the theme's ramp from low to full.
        auto gradient = ctx.createLinearGradient(0, h, 0, zone);
        for (int k = 0; k <= 8; k++) {
          float t = static_cast<float>(k) / 8.0f;
          gradient.addColorStop(t, toCanvas(styles.levelColor(t)));
        }
        ctx.fillStyle = gradient;
      }
      ctx.fillRect(x0, h - fill, x1 - x0, fill);
    }
    // The peak: a line as thick as the display scale, at the peak's height, in the colour of that height.
    if (reading.peak > 0.0f) {
      float y = std::clamp(h - std::clamp(reading.peak, 0.0f, 1.0f) * span, zone, h - thickness);
      Color peak_color = look.color ? *look.color : styles.levelColor(reading.peak);
      ctx.fillStyle = toCanvas(peak_color);
      ctx.fillRect(x0, y, x1 - x0, thickness);
    }
  }

  Picture out = readPicture(drawn, width, height, 1.0f);
  return out.width ? out : blank;
}

std::vector<uint32_t> render(const std::vector<std::optional<SparklineReading>> & columns, const SparklineLook & look, unsigned width,
                             unsigned height, const StyleProvider & styles, float scale) {
  return renderPicture(columns, look, width, height, styles, scale, false).pixels;
}

}  // namespace sparkline_image

}  // namespace cellui
