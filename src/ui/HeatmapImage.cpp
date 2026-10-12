#include "HeatmapImage.h"

#include <Context.h>
#include <ContextCairo.h>

#include <algorithm>
#include <cmath>

namespace heatmap_image {

void cellColor(const Color & background, float saturation, float value, float & r, float & g, float & b) {
  value = std::clamp(value, 0.0f, 1.0f);
  uint8_t lr, lg, lb;
  heatmapHsvToRgb(kHeatmapHue, std::clamp(saturation, 0.0f, 1.0f), 1.0f, lr, lg, lb);
  r = static_cast<float>(background.getRed()) * (1.0f - value) + static_cast<float>(lr) * value;
  g = static_cast<float>(background.getGreen()) * (1.0f - value) + static_cast<float>(lg) * value;
  b = static_cast<float>(background.getBlue()) * (1.0f - value) + static_cast<float>(lb) * value;
}

std::vector<uint32_t> render(const std::vector<float> & brightness, const std::vector<float> & saturation, int cols, int rows,
                             const std::vector<HeatmapChart::Marker> & markers, unsigned width, unsigned height, const StyleProvider & styles) {
  std::vector<uint32_t> out(static_cast<size_t>(width) * height, 0);
  const size_t cells = static_cast<size_t>(std::max(cols, 0)) * static_cast<size_t>(std::max(rows, 0));
  if (width == 0 || height == 0 || cells == 0 || brightness.size() != cells || saturation.size() != cells) return out;

  // The grid as a small image, one pixel per cell, flipped to screen order
  // (row 0 at the top) and with a replicated one-cell border: the scaler
  // fades the edge of an image to transparent, so the border keeps the
  // outermost cells at full strength.
  const unsigned gw = static_cast<unsigned>(cols) + 2, gh = static_cast<unsigned>(rows) + 2;
  std::vector<unsigned char> small(static_cast<size_t>(gw) * gh * 4);
  for (unsigned y = 0; y < gh; y++) {
    int grid_row = rows - 1 - (std::clamp(static_cast<int>(y) - 1, 0, rows - 1));
    for (unsigned x = 0; x < gw; x++) {
      int grid_col = std::clamp(static_cast<int>(x) - 1, 0, cols - 1);
      size_t i = static_cast<size_t>(grid_row) * static_cast<size_t>(cols) + static_cast<size_t>(grid_col);
      float r, g, b;
      cellColor(styles.window_bg_color, saturation[i], brightness[i], r, g, b);
      // Drawn as is, so in the surface's own layout: bytes B, G, R, A
      // (premultiplied, and every pixel is opaque).
      unsigned char * p = &small[(static_cast<size_t>(y) * gw + x) * 4];
      p[0] = static_cast<unsigned char>(b + 0.5f);
      p[1] = static_cast<unsigned char>(g + 0.5f);
      p[2] = static_cast<unsigned char>(r + 0.5f);
      p[3] = 255;
    }
  }

  canvas::ContextCairo ctx(width, height);
  const double sx = static_cast<double>(width) / static_cast<double>(cols), sy = static_cast<double>(height) / static_cast<double>(rows);
  canvas::ImageData image(small.data(), gw, gh, 4);
  ctx.drawImage(image, -sx, -sy, sx * gw, sy * gh);

  ctx.fillStyle = canvas::Color(1.0f, 1.0f, 1.0f, 1.0f);
  for (const auto & m : markers) {
    double mx = std::floor(static_cast<double>(m.u) * width), my = static_cast<double>(height) - 1.0 - std::floor(static_cast<double>(m.v) * height);
    ctx.fillRect(mx - 1.0, my - 1.0, 3.0, 3.0);
  }

  auto result = ctx.getDefaultSurface().createImage(1.0f);
  const canvas::ImageData & data = result->getData();
  if (data.getNumChannels() != 4 || data.getWidth() != width || data.getHeight() != height) return out;
  const unsigned char * src = data.getData();
  for (size_t i = 0; i < out.size(); i++) {
    uint32_t b = src[4 * i], g = src[4 * i + 1], r = src[4 * i + 2], a = src[4 * i + 3];
    (void)a;
    out[i] = r | (g << 8) | (b << 16) | (0xffu << 24); // the image is opaque; stay so
  }
  return out;
}

}  // namespace heatmap_image
