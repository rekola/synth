#include <cellui/ClipLamp.h>

#include <cellui/StageStats.h>

#include <canvas/Context.h>
#include "NativeContext.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>
#include <vector>

namespace cellui {

namespace clip_lamp_image {

namespace {

canvas::Color toCanvas(const Color & c) { return canvas::Color(c.r(), c.g(), c.b(), 1.0f); }

}  // namespace

Picture renderPicture(bool lit, unsigned width, unsigned height, const Color & background, const StyleProvider & styles, bool prefer_float) {
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
  // On a float picture, which only a window takes, the lamp is on nothing: the glow of a lit one then meets the
  // cells under it the same way inside the picture as outside it, with no seam at its edge. An 8-bit picture is
  // opaque, as a terminal's encoders want.
  if (!drawn.is_float) {
    ctx.fillStyle = toCanvas(background);
    ctx.fillRect(0, 0, w, h);
  }
  // A disc a little smaller than the cells, so a lit one has room to glow and a dim one sits in its place.
  const double radius = 0.4 * std::min(w, h);
  ctx.beginPath().arc(w / 2.0, h / 2.0, radius, 0.0, 6.283185307179586).closePath();
  ctx.fillStyle = toCanvas(styles.clipLedColor(lit, background));
  ctx.fill();

  Picture out = readPicture(drawn, width, height, 1.0f);
  return out.width ? out : blank;
}

}  // namespace clip_lamp_image

namespace {

// The last few pictures made: a lamp is one of two colours at one size, and is drawn again with every
// frame its meter redraws.
struct CachedLamp {
  bool lit, hdr;
  unsigned width, height;
  Color background, full;
  Picture picture;
};

const Picture & lampPicture(bool lit, unsigned width, unsigned height, const Color & background, const StyleProvider & styles, bool hdr) {
  static thread_local std::vector<CachedLamp> cache;
  for (const CachedLamp & c : cache) {
    if (c.lit == lit && c.hdr == hdr && c.width == width && c.height == height && c.background == background && c.full == styles.level_full_color) return c.picture;
  }
  if (cache.size() >= 8) cache.erase(cache.begin());
  cache.push_back(CachedLamp{lit, hdr, width, height, background, styles.level_full_color,
                             clip_lamp_image::renderPicture(lit, width, height, background, styles, hdr)});
  return cache.back().picture;
}

}  // namespace

void drawClipLamp(Plane & plane, int row, int col, bool lit, const Color & background, int clip_top, int clip_bottom, uint64_t id) {
  int cell_h = 0, cell_w = 0;
  if (!plane.cellPixelSize(cell_h, cell_w) || cell_h <= 0 || cell_w <= 0) {
    drawCellClipLamp(plane, row, col, lit, background, clip_top, clip_bottom);
    return;
  }
  const Picture & picture = lampPicture(lit, static_cast<unsigned>(kClipLampCols * cell_w), static_cast<unsigned>(cell_h), background, plane.getStyles(), plane.hdrPictures());
  drawPictureAt(plane, row, col, picture, id, clip_top, clip_bottom);
}

void drawCellClipLamp(Plane & plane, int row, int col, bool lit, const Color & background, int clip_top, int clip_bottom) {
  if (row < clip_top || row >= clip_bottom) return;
  plane.setFgColor(plane.getStyles().clipLedColor(lit, background));
  plane.setBgColor(background);
  plane.putstr(row, col, kClipLampGlyph);
}

}  // namespace cellui
