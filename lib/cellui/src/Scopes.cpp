#include <cellui/Scopes.h>

#include <cellui/CellRasterizer.h>

#include "HeatmapGrid.h"

#include <cellui/StyleProvider.h>
#include <cellui/Plane.h>
#include <cellui/Utf8.h>

#include <algorithm>
#include <string>
#include <vector>

namespace cellui {

using namespace heatmap_grid;

namespace {


// The heatmap in cells, no pixel graphics needed: paints one Unicode block glyph per character cell - a sextant
// (2x3 sub-cells, SubcellGlyphs.h's sextantCodepoint()) when
// Plane::canRenderSextants() confirms the terminal supports Unicode
// 13's sextant range, else a quadrant (2x2 sub-cells, kQuadrantCodepoints),
// read once at construction (the terminal's capabilities don't change
// mid-session) - tripling (sextants) or doubling (quadrants) the
// effective vertical resolution over a flat one-color-per-cell approach.
// Each cell's foreground/background pair is the optimal 2-color
// quantization (quantizeToTwoColors) of its real sub-samples, not a naive
// corner pick or a fixed brightness threshold. 24-bit truecolor is
// near-universally supported even on terminals without sixel/Kitty image
// support, unlike the braille dot glyphs TerminalChart uses for 1D bars -
// a heatmap wants a genuine 2D color field, not a thresholded on/off
// pattern along one axis. Resamples (resampleGrid(), above) from the
// logical gridCols()xgridRows() grid to however many sub-cells this
// widget actually has (2x the character-cell count horizontally always,
// 3x or 2x vertically depending on sextant support).
class CellHeatmapChart : public HeatmapChart {
public:
  CellHeatmapChart(Plane & parent, int grid_cols, int grid_rows)
    : HeatmapChart(parent, grid_cols, grid_rows) {
    use_sextants_ = getPlane().canRenderSextants();
  }

  void setGrid(const std::vector<float> & brightness, const std::vector<float> & saturation) override {
    brightness_ = brightness;
    saturation_ = saturation;
  }

  void setMarkers(std::vector<Marker> markers) override {
    markers_ = std::move(markers);
  }

  void commit() override {
    if (brightness_.empty()) return;
    auto & background = getPlane().getStyles().window_bg_color;
    auto [rows, cols] = getDim();
    int usable_rows = footer_label_.empty() ? rows : rows - 1;
    if (usable_rows <= 0 || cols <= 0) return;

    int sub_rows = use_sextants_ ? 3 : 2;
    int vcols = cols * 2, vrows = usable_rows * sub_rows; // sextant/quadrant sub-cell resolution
    std::vector<float> agg_brightness, agg_saturation;
    resampleGrid(brightness_, saturation_, gridCols(), gridRows(), vcols, vrows, agg_brightness, agg_saturation);

    std::vector<SubcellRgb> samples(static_cast<size_t>(sub_rows * 2));
    for (int sy = 0; sy < usable_rows; sy++) {
      for (int sx = 0; sx < cols; sx++) {
        // Gather this character cell's sub-samples (row-major - top-left
        // first, matching kQuadrantCodepoints'/sextantCodepoint()'s own bit
        // order) from the resampled sub-grid. Screen row 0 is the TOP of
        // the widget, but sub-grid row 0 is the BOTTOM (resampleGrid()'s
        // contract, inherited from HeatmapChart.h's own setGrid()) - flip
        // vertically.
        int bit = 0;
        for (int qy = 0; qy < sub_rows; qy++) {
          int vy_from_top = sy * sub_rows + qy;
          int vy = vrows - 1 - vy_from_top;
          for (int qx = 0; qx < 2; qx++) {
            int vx = sx * 2 + qx;
            size_t idx = static_cast<size_t>(vy * vcols + vx);
            samples[static_cast<size_t>(bit)] = heatmapCellColor(background, agg_saturation[idx], agg_brightness[idx]);
            bit++;
          }
        }

        SubcellRgb on_color, off_color;
        int mask = quantizeToTwoColors(samples, on_color, off_color);

        setFgColor(static_cast<int>(on_color.r), static_cast<int>(on_color.g), static_cast<int>(on_color.b));
        setBgColor(static_cast<int>(off_color.r), static_cast<int>(off_color.g), static_cast<int>(off_color.b));
        putstr(sy, sx, utf8::encodeCodepoint(use_sextants_ ? sextantCodepoint(mask) : kQuadrantCodepoints[mask]));
      }
    }
    setFgColor(255, 255, 255);
    setBgColor(background); // don't leave the last cell's colors "stuck" for whatever draws next (e.g. the footer label below)

    for (auto & marker : markers_) {
      int sx = static_cast<int>(marker.u * static_cast<float>(cols));
      int sy = usable_rows - 1 - static_cast<int>(marker.v * static_cast<float>(usable_rows));
      if (sx < 0 || sx >= cols || sy < 0 || sy >= usable_rows) continue;
      putstr(sy, sx, '+');
    }

    for (auto & label : axisLabels(usable_rows, cols)) {
      std::string text = label.text;
      for (int c = 0; c < static_cast<int>(text.size()); c++) {
        int sx = label.col + c;
        if (sx < 0 || sx >= cols) continue;

        // This character's own sub-samples, collapsed to a single mean
        // color - the exception to the normal two-color sextant/quadrant
        // blend that a label-covered cell gets (see axisLabels()'s own
        // comment above).
        SubcellRgb sum{0, 0, 0};
        for (int qy = 0; qy < sub_rows; qy++) {
          int vy_from_top = label.row * sub_rows + qy;
          int vy = vrows - 1 - vy_from_top;
          for (int qx = 0; qx < 2; qx++) {
            int vx = sx * 2 + qx;
            size_t idx = static_cast<size_t>(vy * vcols + vx);
            SubcellRgb rgb = heatmapCellColor(background, agg_saturation[idx], agg_brightness[idx]);
            sum.r += rgb.r; sum.g += rgb.g; sum.b += rgb.b;
          }
        }
        float sample_count = static_cast<float>(sub_rows * 2);
        SubcellRgb mean{sum.r / sample_count, sum.g / sample_count, sum.b / sample_count};

        uint8_t fr, fg, fb;
        labelForegroundColor(mean, fr, fg, fb);
        setFgColor(fr, fg, fb);
        setBgColor(static_cast<int>(mean.r), static_cast<int>(mean.g), static_cast<int>(mean.b));
        putstr(label.row, sx, std::string(1, text[static_cast<size_t>(c)]));
      }
    }

    if (!footer_label_.empty()) {
      putstr(rows - 1, 0, footer_label_);
    }
  }

private:
  bool use_sextants_;
  std::vector<float> brightness_, saturation_;
  std::vector<Marker> markers_;
};

}  // namespace

void drawCellVuMeter(Plane & plane, int row, int col, int rows, const VuReading & reading, const VuMeterLook & look, int clip_top,
                     int clip_bottom) {
  if (rows <= 0) return;
  auto & styles = plane.getStyles();
  auto cells = level_meter::verticalBar(reading.fraction, rows, reading.peak);
  plane.setBgColor(look.background);
  for (int i = 0; i < rows; i++) {
    int y = row + i;
    if (y < clip_top || y >= clip_bottom) continue;
    // Shaded by the height of this cell; i = 0 is the top one.
    Color fg = reading.clipping ? styles.meter_clip_color
               : look.color     ? *look.color
                                : styles.meterColor((static_cast<float>(rows - i) - 0.5f) / static_cast<float>(rows));
    plane.setFgColor(fg);
    plane.putstr(y, col, cells[static_cast<size_t>(i)]);
  }
}

namespace {

class CellScopes : public Scopes {
 public:
  std::shared_ptr<SpectrumMeter> spectrum(Plane & parent) const override { return std::make_shared<SpectrumMeter>(parent); }
  std::shared_ptr<HeatmapChart> heatmap(Plane & parent, int grid_cols, int grid_rows) const override {
    return std::make_shared<CellHeatmapChart>(parent, grid_cols, grid_rows);
  }
  std::shared_ptr<ChannelMeter> channelMeter(Plane & parent) const override { return std::make_shared<ChannelMeter>(parent); }
  void drawVuMeter(Plane & plane, int row, int col, int rows, const VuReading & reading, const VuMeterLook & look, int clip_top,
                   int clip_bottom, uint64_t) const override {
    drawCellVuMeter(plane, row, col, rows, reading, look, clip_top, clip_bottom);
  }
};

}  // namespace

std::unique_ptr<Scopes> cellScopes() { return std::make_unique<CellScopes>(); }

#ifndef CELLUI_PIXEL_SCOPES
std::unique_ptr<Scopes> pixelScopes() { return nullptr; }
std::unique_ptr<CellRasterizer> canvasRasterizer(const std::string &) { return nullptr; }
#endif

}  // namespace cellui
