#include <cellui/Scopes.h>

#include "HeatmapGrid.h"

#include <cellui/HeatmapImage.h>
#include <cellui/SpectrumImage.h>
#include <cellui/StyleProvider.h>
#include <cellui/UIPlane.h>

#include <algorithm>
#include <vector>

namespace cellui {

using namespace heatmap_grid;

namespace {

// 5x7 glyphs for the characters axisLabels() uses, row-major ('#' = ink).
const char * pixelGlyph(char c) {
  switch (c) {
    case '+': return "....." "..#.." "..#.." "#####" "..#.." "..#.." ".....";
    case '-': return "....." "....." "....." "#####" "....." "....." ".....";
    case '0': return ".###." "#...#" "#..##" "#.#.#" "##..#" "#...#" ".###.";
    case '1': return "..#.." ".##.." "..#.." "..#.." "..#.." "..#.." ".###.";
    case '8': return ".###." "#...#" "#...#" ".###." "#...#" "#...#" ".###.";
    case '9': return ".###." "#...#" "#...#" ".####" "....#" "#...#" ".###.";
    default: return nullptr;
  }
}

// Draws a label into the ABGR pixel buffer (row 0 = top), each glyph
// scaled to the character cell and blended in white so the heatmap
// still shows through.
void drawPixelLabel(std::vector<uint32_t> & buffer, unsigned pxx, unsigned pxy, const AxisLabel & label, unsigned cell_w, unsigned cell_h) {
  // About half the cell's height: small, unobtrusive marks.
  const unsigned sx = std::max(1u, cell_w / 12), sy = std::max(1u, cell_h / 16);
  const unsigned glyph_w = 5 * sx, glyph_h = 7 * sy;
  for (unsigned c = 0; label.text[c]; c++) {
    const char * glyph = pixelGlyph(label.text[c]);
    if (!glyph) continue;
    long x0 = static_cast<long>(label.col + static_cast<int>(c)) * cell_w + (cell_w > glyph_w ? (cell_w - glyph_w) / 2 : 0);
    long y0 = static_cast<long>(label.row) * cell_h + (cell_h > glyph_h ? (cell_h - glyph_h) / 2 : 0);
    for (unsigned gy = 0; gy < 7; gy++) {
      for (unsigned gx = 0; gx < 5; gx++) {
        if (glyph[gy * 5 + gx] != '#') continue;
        for (unsigned dy = 0; dy < sy; dy++) {
          for (unsigned dx = 0; dx < sx; dx++) {
            long x = x0 + static_cast<long>(gx * sx + dx), y = y0 + static_cast<long>(gy * sy + dy);
            if (x < 0 || y < 0 || x >= static_cast<long>(pxx) || y >= static_cast<long>(pxy)) continue;
            uint32_t & pixel = buffer[static_cast<size_t>(y) * pxx + static_cast<size_t>(x)];
            auto mix = [](uint32_t channel) { return static_cast<uint32_t>(kLabelForegroundAlpha * 255.0f + (1.0f - kLabelForegroundAlpha) * static_cast<float>(channel)); };
            pixel = (0xffu << 24) | (mix((pixel >> 16) & 0xffu) << 16) | (mix((pixel >> 8) & 0xffu) << 8) | mix(pixel & 0xffu);
          }
        }
      }
    }
  }
}

// The heatmap as the plane's image, drawn with canvas at the plane's size
// in pixels.
class PixelHeatmapChart : public HeatmapChart {
public:
  PixelHeatmapChart(UIPlane & parent, int grid_cols, int grid_rows)
    : HeatmapChart(parent, grid_cols, grid_rows) { }

  void setGrid(const std::vector<float> & brightness, const std::vector<float> & saturation) override {
    brightness_ = brightness;
    saturation_ = saturation;
  }

  void setMarkers(std::vector<Marker> markers) override {
    markers_ = std::move(markers);
  }

  void commit() override {
    if (brightness_.empty()) return;

    int px_h = 0, px_w = 0;
    if (!getPlane().pixelSize(px_h, px_w)) return;
    auto [plane_rows, plane_cols] = getDim();
    unsigned pxy = static_cast<unsigned>(px_h), pxx = static_cast<unsigned>(px_w);
    unsigned celldimy = std::max(1u, pxy / static_cast<unsigned>(std::max(1, plane_rows)));
    unsigned celldimx = std::max(1u, pxx / static_cast<unsigned>(std::max(1, plane_cols)));

    // A footer row, when there is one, stays text below the image.
    if (!footer_label_.empty() && pxy > celldimy) pxy -= celldimy;

    auto buffer = heatmap_image::render(brightness_, saturation_, gridCols(), gridRows(), markers_, pxx, pxy, getPlane().getStyles());

    // Axis labels go into the image itself: text in the plane's cells would
    // be hidden under the bitmap.
    {
      auto [label_rows, label_cols] = getDim();
      int usable_rows = footer_label_.empty() ? static_cast<int>(label_rows) : static_cast<int>(label_rows) - 1;
      for (auto & label : axisLabels(usable_rows, static_cast<int>(label_cols))) {
        drawPixelLabel(buffer, pxx, pxy, label, celldimx, celldimy);
      }
    }

    getPlane().drawImage(buffer, static_cast<int>(pxx), static_cast<int>(pxy));
  }

private:
  std::vector<float> brightness_, saturation_;
  std::vector<Marker> markers_;
};

// The spectrum as the plane's image: one level per pixel column, drawn
// with canvas.
class PixelSpectrumMeter : public SpectrumMeter {
public:
  using SpectrumMeter::SpectrumMeter;

protected:
  size_t barCount(int cols) override {
    int pxy = 0, pxx = 0;
    if (getPlane().pixelSize(pxy, pxx)) return static_cast<size_t>(pxx);
    return SpectrumMeter::barCount(cols);
  }

  void drawBars(const std::vector<float> & levels) override {
    int pxy = 0, pxx = 0;
    if (!getPlane().pixelSize(pxy, pxx) || levels.size() != static_cast<size_t>(pxx)) {
      SpectrumMeter::drawBars(levels);
      return;
    }
    // Opaque, so a pixel never composites against the raw terminal background.
    auto buffer = spectrum_image::render(levels, static_cast<unsigned>(pxx), static_cast<unsigned>(pxy), getPlane().getStyles(),
                                         static_cast<float>(getPlane().displayScale()));
    getPlane().drawImage(buffer, pxx, pxy);
  }
};

class PixelScopes : public Scopes {
 public:
  std::shared_ptr<SpectrumMeter> spectrum(UIPlane & parent) const override { return std::make_shared<PixelSpectrumMeter>(parent); }
  std::shared_ptr<HeatmapChart> heatmap(UIPlane & parent, int grid_cols, int grid_rows) const override {
    return std::make_shared<PixelHeatmapChart>(parent, grid_cols, grid_rows);
  }
  std::shared_ptr<ChannelMeter> channelMeter(UIPlane & parent) const override { return std::make_shared<ChannelMeter>(parent); }
};

}  // namespace

std::unique_ptr<Scopes> pixelScopes() { return std::make_unique<PixelScopes>(); }

}  // namespace cellui
