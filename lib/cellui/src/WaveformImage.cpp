#include <cellui/StageStats.h>
#include <cellui/WaveformImage.h>

#include <canvas/Context.h>
#include "NativeContext.h"

#include <algorithm>
#include <cmath>

namespace cellui {

namespace waveform_image {

namespace {

// A colour as canvas takes it from a linear Rec.2020 context: the components as they are.
canvas::Color toCanvas(const Color & c) { return canvas::Color(c.r(), c.g(), c.b(), 1.0f); }

}  // namespace

Picture renderPicture(const std::vector<float> & slices, const Color & fg, const Color & bg, const Color & guide, unsigned width, unsigned height,
                      float scale, bool prefer_float) {
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

  // Each slice is a band of the height, its bar centred across the width, lit past white as it nears
  // full scale (where the waveform is about to clip).
  const size_t count = std::max<size_t>(1, slices.size());
  for (size_t i = 0; i < slices.size(); i++) {
    float level = std::clamp(slices[i], 0.0f, 1.0f);
    if (level <= 0.0f) continue;
    ctx.fillStyle = toCanvas(fg * StyleProvider::litGain(level, kHdrStart, kHdrGain));
    float y0 = h * static_cast<float>(i) / static_cast<float>(count), y1 = h * static_cast<float>(i + 1) / static_cast<float>(count);
    float bar = level * w;
    ctx.fillRect((w - bar) / 2.0f, y0, bar, y1 - y0);
  }

  Picture out = readPicture(drawn, width, height, 1.0f);
  return out.width ? out : blank;
}

std::vector<uint32_t> render(const std::vector<float> & slices, const Color & fg, const Color & bg, const Color & guide, unsigned width,
                             unsigned height, float scale) {
  return renderPicture(slices, fg, bg, guide, width, height, scale, false).pixels;
}

}  // namespace waveform_image

}  // namespace cellui
