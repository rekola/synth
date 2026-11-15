#include <cellui/StageStats.h>
#include <cellui/HeatmapImage.h>

#include <cellui/ImageScale.h>

#include <canvas/Context.h>
#include "NativeContext.h"

#include <algorithm>
#include <cmath>

namespace cellui {

namespace heatmap_image {

namespace {

// The labels as canvas draws them: white at kLabelAlpha on a transparent canvas. `bgra` is the surface as it
// holds it, premultiplied B, G, R, A, `width` pixels a row, empty when there is nothing to draw.
struct LabelLayer {
  std::vector<unsigned char> bgra;
  unsigned width = 0, height = 0;
};

LabelLayer drawLabelLayer(unsigned width, unsigned height, const std::vector<HeatmapChart::Label> & labels, float label_px) {
  LabelLayer layer;
  if (labels.empty() || label_px <= 0.0f) return layer;
  auto context = cellui::nativeContext(width, height);
  canvas::Context & ctx = *context;
  ctx.fillStyle = canvas::Color(1.0f, 1.0f, 1.0f, kLabelAlpha);
  ctx.font.family = "sans-serif";
  ctx.font.size = label_px;
  ctx.textBaseline = canvas::TextBaseline::MIDDLE;
  ctx.textAlign = canvas::TextAlign::LEFT;
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  for (const auto & label : labels) {
    float text_w = static_cast<float>(ctx.measureText(label.text).width);
    float x = std::clamp(label.u * w - label.align_x * text_w, 0.0f, std::max(0.0f, w - text_w));
    float top = std::clamp((1.0f - label.v) * h - (1.0f - label.align_y) * label_px, 0.0f, std::max(0.0f, h - label_px));
    ctx.fillText(label.text, x, top + label_px / 2.0f);
  }
  auto image = ctx.getDefaultSurface().createImage(1.0f);
  const canvas::ImageData & data = image->getData();
  if (data.getNumChannels() != 4 || data.getWidth() < width || data.getHeight() < height) return layer;
  layer.width = width;
  layer.height = height;
  layer.bgra.resize(static_cast<size_t>(width) * height * 4);
  const size_t stride = static_cast<size_t>(data.getWidth()) * 4;
  for (unsigned y = 0; y < height; y++) std::copy_n(data.getData() + y * stride, static_cast<size_t>(width) * 4, &layer.bgra[static_cast<size_t>(y) * width * 4]);
  return layer;
}

// The label over a pixel is its own colour plus what the pixel keeps of the light it lets through.
inline uint32_t overByte(uint32_t below, uint32_t above, uint32_t alpha) { return std::min(255u, above + (below * (255u - alpha) + 127u) / 255u); }

// Writes the labels over `out` (RGBA, row 0 at the top).
void drawLabels(std::vector<uint32_t> & out, unsigned width, unsigned height, const std::vector<HeatmapChart::Label> & labels, float label_px) {
  LabelLayer layer = drawLabelLayer(width, height, labels, label_px);
  if (layer.bgra.empty()) return;
  for (size_t i = 0; i < static_cast<size_t>(width) * height; i++) {
    const unsigned char * p = &layer.bgra[i * 4];
    if (p[3] == 0) continue;
    uint32_t & px = out[i];
    px = overByte(px & 0xffu, p[2], p[3]) | (overByte((px >> 8) & 0xffu, p[1], p[3]) << 8) | (overByte((px >> 16) & 0xffu, p[0], p[3]) << 16) | (0xffu << 24);
  }
}

// The same over float pixels (linear Rec.2020, 4 floats each): a pixel under a label is laid over as its 8-bit
// self is, so a label looks the same on either picture.
void drawLabelsHdr(std::vector<float> & out, unsigned width, unsigned height, const std::vector<HeatmapChart::Label> & labels, float label_px) {
  LabelLayer layer = drawLabelLayer(width, height, labels, label_px);
  if (layer.bgra.empty()) return;
  for (size_t i = 0; i < static_cast<size_t>(width) * height; i++) {
    const unsigned char * p = &layer.bgra[i * 4];
    if (p[3] == 0) continue;
    float * px = &out[i * 4];
    auto below = Color::linear(px[0], px[1], px[2]).toSrgb8();
    Color over(static_cast<int>(overByte(below[0], p[2], p[3])), static_cast<int>(overByte(below[1], p[1], p[3])), static_cast<int>(overByte(below[2], p[0], p[3])));
    px[0] = over.r();
    px[1] = over.g();
    px[2] = over.b();
  }
}

}  // namespace

Picture renderPicture(const std::vector<float> & brightness, const std::vector<float> & saturation, int cols, int rows,
                      const std::vector<HeatmapChart::Marker> & markers, const std::vector<HeatmapChart::Label> & labels, unsigned width,
                      unsigned height, const StyleProvider & styles, float label_px, bool prefer_float) {
  StageTimer timer(Stage::PICTURE_RENDER);
  Picture out;
  out.width = static_cast<int>(width);
  out.height = static_cast<int>(height);
  out.pixels.assign(static_cast<size_t>(width) * height, 0);
  const size_t cells = static_cast<size_t>(std::max(cols, 0)) * static_cast<size_t>(std::max(rows, 0));
  if (width == 0 || height == 0 || cells == 0 || brightness.size() != cells || saturation.size() != cells) return out;

  // The cells' colours in screen order (row 0 at the top); grid row 0 is the bottom (HeatmapChart::setGrid()).
  std::vector<Color> colours(cells);
  for (int y = 0; y < rows; y++) {
    for (int x = 0; x < cols; x++) {
      size_t i = static_cast<size_t>(rows - 1 - y) * static_cast<size_t>(cols) + static_cast<size_t>(x);
      colours[static_cast<size_t>(y) * static_cast<size_t>(cols) + static_cast<size_t>(x)] = litCellColor(styles.window_bg_color, saturation[i], brightness[i]);
    }
  }
  auto cell_of = [&](unsigned v, unsigned size, int count) { return std::min(count - 1, static_cast<int>(static_cast<int64_t>(v) * count / static_cast<int64_t>(size))); };
  auto ink = styles.window_fg_color;
  auto marker_pixel = [&](const HeatmapChart::Marker & m, int & mx, int & my) {
    mx = static_cast<int>(std::floor(static_cast<double>(m.u) * width));
    my = static_cast<int>(height) - 1 - static_cast<int>(std::floor(static_cast<double>(m.v) * height));
  };

  if (prefer_float) {
    // The grid enlarged by nearest neighbour into floats: each cell a block of one colour.
    out.pixels.clear();
    out.hdr.resize(static_cast<size_t>(width) * height * 4);
    std::vector<int> column(width);
    for (unsigned x = 0; x < width; x++) column[x] = cell_of(x, width, cols);
    for (unsigned y = 0; y < height; y++) {
      const Color * row = &colours[static_cast<size_t>(cell_of(y, height, rows)) * static_cast<size_t>(cols)];
      float * dest = &out.hdr[static_cast<size_t>(y) * width * 4];
      for (unsigned x = 0; x < width; x++) {
        const Color & c = row[column[x]];
        dest[x * 4] = c.r();
        dest[x * 4 + 1] = c.g();
        dest[x * 4 + 2] = c.b();
        dest[x * 4 + 3] = 1.0f;
      }
    }
    for (const auto & m : markers) {
      int mx, my;
      marker_pixel(m, mx, my);
      for (int y = std::max(0, my - 1); y <= std::min(static_cast<int>(height) - 1, my + 1); y++) {
        for (int x = std::max(0, mx - 1); x <= std::min(static_cast<int>(width) - 1, mx + 1); x++) {
          float * px = &out.hdr[(static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4];
          px[0] = ink.r();
          px[1] = ink.g();
          px[2] = ink.b();
        }
      }
    }
    drawLabelsHdr(out.hdr, width, height, labels, label_px);
    return out;
  }

  // The same as 8-bit pixels: the grid as a small image, one pixel per cell, enlarged by nearest neighbour.
  Image small;
  small.width = cols;
  small.height = rows;
  small.pixels.resize(cells);
  for (size_t i = 0; i < cells; i++) {
    auto rgb = colours[i].toSrgb8();
    small.pixels[i] = Image::pack(rgb[0], rgb[1], rgb[2]);
  }
  const Image scaled = scaleImageNearest(small, static_cast<int>(width), static_cast<int>(height));
  for (size_t i = 0; i < out.pixels.size(); i++) out.pixels[i] = scaled.pixels[i] | (0xffu << 24);  // the image is opaque; stay so

  auto ink8 = ink.toSrgb8();
  const uint32_t marker_colour = Image::pack(ink8[0], ink8[1], ink8[2]);
  for (const auto & m : markers) {
    int mx, my;
    marker_pixel(m, mx, my);
    for (int y = std::max(0, my - 1); y <= std::min(static_cast<int>(height) - 1, my + 1); y++) {
      for (int x = std::max(0, mx - 1); x <= std::min(static_cast<int>(width) - 1, mx + 1); x++) out.pixels[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = marker_colour;
    }
  }
  drawLabels(out.pixels, width, height, labels, label_px);
  return out;
}

std::vector<uint32_t> render(const std::vector<float> & brightness, const std::vector<float> & saturation, int cols, int rows,
                             const std::vector<HeatmapChart::Marker> & markers, const std::vector<HeatmapChart::Label> & labels,
                             unsigned width, unsigned height, const StyleProvider & styles, float label_px) {
  return renderPicture(brightness, saturation, cols, rows, markers, labels, width, height, styles, label_px, false).pixels;
}

}  // namespace heatmap_image

}  // namespace cellui
