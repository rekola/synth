#include <cellui/StageStats.h>
#include <cellui/HeatmapImage.h>

#include <cellui/ImageScale.h>

#include <algorithm>
#include <cmath>

namespace cellui {

namespace heatmap_image {

std::vector<uint32_t> render(const std::vector<float> & brightness, const std::vector<float> & saturation, int cols, int rows,
                             const std::vector<HeatmapChart::Marker> & markers, unsigned width, unsigned height, const StyleProvider & styles) {
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
  return out;
}

}  // namespace heatmap_image

}  // namespace cellui
