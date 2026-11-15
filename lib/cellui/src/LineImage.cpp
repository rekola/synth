#include <cellui/StageStats.h>
#include <cellui/LineImage.h>

#include <canvas/Context.h>
#include "NativeContext.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cellui {

namespace line_image {

namespace {

// A colour as canvas takes it from a linear Rec.2020 context: the components as they are.
canvas::Color toCanvas(const Color & c, float alpha = 1.0f) { return canvas::Color(c.r(), c.g(), c.b(), alpha); }

}  // namespace

Picture renderPicture(const std::vector<float> & levels, unsigned width, unsigned height, const StyleProvider & styles, float scale,
                      bool prefer_float) {
  StageTimer timer(Stage::PICTURE_RENDER);
  Picture blank;
  blank.width = static_cast<int>(width);
  blank.height = static_cast<int>(height);
  blank.pixels.assign(static_cast<size_t>(width) * height, 0);
  if (width == 0 || height == 0 || levels.size() != width) return blank;

  // Drawn in canvas's logical units at the display scale, on a surface of exactly width x height pixels.
  scale = std::max(1.0f, scale);
  LinearContext drawn = nativeLinearContext(width, height, scale, prefer_float);
  if (!drawn.context) return blank;
  canvas::Context & ctx = *drawn.context;
  const float w = static_cast<float>(width) / scale, h = static_cast<float>(height) / scale;
  ctx.fillStyle = toCanvas(styles.window_bg_color);
  ctx.fillRect(0, 0, std::ceil(w), std::ceil(h));

  // The curve, one point per pixel column centre, a unit in from the top and
  // bottom edges.
  const float inset = std::min(1.0f, h / 4.0f);
  auto x_at = [&](unsigned x) { return (static_cast<double>(x) + 0.5) / scale; };
  auto y_at = [&](unsigned x) { return h - (levels[x] * (h - 2.0f * inset) + inset); };
  ctx.beginPath().moveTo(0.0, h);
  for (unsigned x = 0; x < width; x++) ctx.lineTo(x_at(x), y_at(x));
  ctx.lineTo(w, h).closePath();

  auto fill = ctx.createLinearGradient(0, h, 0, 0);
  fill.addColorStop(0.0f, toCanvas(styles.lineFillColor(0.0f), 0.22f));
  fill.addColorStop(1.0f, toCanvas(styles.lineFillColor(1.0f), 0.22f));
  ctx.fillStyle = fill;
  ctx.fill();

  ctx.beginPath().moveTo(x_at(0), y_at(0));
  for (unsigned x = 1; x < width; x++) ctx.lineTo(x_at(x), y_at(x));
  auto stroke = ctx.createLinearGradient(0, h, 0, 0);
  // The ramp's top is lit past white, so it is more than its two ends.
  for (int i = 0; i <= 8; i++) stroke.addColorStop(static_cast<float>(i) / 8.0f, toCanvas(styles.lineColor(static_cast<float>(i) / 8.0f)));
  ctx.strokeStyle = stroke;
  ctx.lineWidth = 1.5f;
  ctx.stroke();

  Picture out = readPicture(drawn, width, height, scale);
  return out.width ? out : blank;
}

std::vector<uint32_t> render(const std::vector<float> & levels, unsigned width, unsigned height, const StyleProvider & styles,
                             float scale) {
  return renderPicture(levels, width, height, styles, scale, false).pixels;
}

}  // namespace line_image

}  // namespace cellui
