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

// Writes the labels over `out` (RGBA, row 0 at the top): drawn white at
// kLabelAlpha on a transparent canvas, which is then laid over the pixels.
void drawLabels(std::vector<uint32_t> & out, unsigned width, unsigned height, const std::vector<HeatmapChart::Label> & labels, float label_px) {
  if (labels.empty() || label_px <= 0.0f) return;
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

  // The surface holds premultiplied B, G, R, A: the label over a pixel is its
  // own colour plus what the pixel keeps of the light it lets through.
  auto image = ctx.getDefaultSurface().createImage(1.0f);
  const canvas::ImageData & data = image->getData();
  if (data.getNumChannels() != 4 || data.getWidth() < width || data.getHeight() < height) return;
  const unsigned char * src = data.getData();
  const size_t stride = static_cast<size_t>(data.getWidth()) * 4;
  for (unsigned y = 0; y < height; y++) {
    for (unsigned x = 0; x < width; x++) {
      const unsigned char * p = src + y * stride + static_cast<size_t>(x) * 4;
      if (p[3] == 0) continue;
      uint32_t & px = out[static_cast<size_t>(y) * width + x];
      const uint32_t keep = 255u - p[3];
      auto over = [&](uint32_t below, uint32_t above) { return std::min(255u, above + (below * keep + 127u) / 255u); };
      px = over(px & 0xffu, p[2]) | (over((px >> 8) & 0xffu, p[1]) << 8) | (over((px >> 16) & 0xffu, p[0]) << 16) | (0xffu << 24);
    }
  }
}

}  // namespace

std::vector<uint32_t> render(const std::vector<float> & brightness, const std::vector<float> & saturation, int cols, int rows,
                             const std::vector<HeatmapChart::Marker> & markers, const std::vector<HeatmapChart::Label> & labels,
                             unsigned width, unsigned height, const StyleProvider & styles, float label_px) {
  StageTimer timer(Stage::PICTURE_RENDER);
  std::vector<uint32_t> out(static_cast<size_t>(width) * height, 0);
  const size_t cells = static_cast<size_t>(std::max(cols, 0)) * static_cast<size_t>(std::max(rows, 0));
  if (width == 0 || height == 0 || cells == 0 || brightness.size() != cells || saturation.size() != cells) return out;

  // The grid as a small image, one pixel per cell, flipped to screen order (row 0 at the top), enlarged by
  // nearest neighbour: each cell is a block of one colour.
  Image small;
  small.width = cols;
  small.height = rows;
  small.pixels.resize(cells);
  for (int y = 0; y < rows; y++) {
    for (int x = 0; x < cols; x++) {
      size_t i = static_cast<size_t>(rows - 1 - y) * static_cast<size_t>(cols) + static_cast<size_t>(x);
      float r, g, b;
      cellColor(styles.window_bg_color, saturation[i], brightness[i], r, g, b);
      small.pixels[static_cast<size_t>(y) * static_cast<size_t>(cols) + static_cast<size_t>(x)] =
        Image::pack(static_cast<uint8_t>(r + 0.5f), static_cast<uint8_t>(g + 0.5f), static_cast<uint8_t>(b + 0.5f));
    }
  }
  const Image scaled = scaleImageNearest(small, static_cast<int>(width), static_cast<int>(height));
  for (size_t i = 0; i < out.size(); i++) out[i] = scaled.pixels[i] | (0xffu << 24);  // the image is opaque; stay so

  auto ink = styles.window_fg_color.toSrgb8();
  const uint32_t marker_colour = Image::pack(ink[0], ink[1], ink[2]);
  for (const auto & m : markers) {
    int mx = static_cast<int>(std::floor(static_cast<double>(m.u) * width)), my = static_cast<int>(height) - 1 - static_cast<int>(std::floor(static_cast<double>(m.v) * height));
    for (int y = std::max(0, my - 1); y <= std::min(static_cast<int>(height) - 1, my + 1); y++) {
      for (int x = std::max(0, mx - 1); x <= std::min(static_cast<int>(width) - 1, mx + 1); x++) out[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = marker_colour;
    }
  }
  drawLabels(out, width, height, labels, label_px);
  return out;
}

}  // namespace heatmap_image

}  // namespace cellui
