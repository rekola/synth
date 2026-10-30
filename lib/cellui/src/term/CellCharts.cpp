#include <cellui/term/CellCharts.h>

#include "HeatmapGrid.h"

#include <cellui/Plane.h>
#include <cellui/StyleProvider.h>
#include <cellui/Utf8.h>
#include <cellui/term/GlyphMeter.h>
#include <cellui/term/SubcellGlyphs.h>

#include <algorithm>
#include <string>
#include <vector>

namespace cellui {

using namespace heatmap_grid;

namespace term {

namespace {

// The heatmap in cells, no pixel graphics needed: paints one Unicode block glyph per character cell - a sextant
// (2x3 sub-cells, SubcellGlyphs.h's sextantCodepoint()) when
// the display reports it supports Unicode
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
  CellHeatmapChart(Plane & parent, int grid_cols, int grid_rows, bool sextants)
    : HeatmapChart(parent, grid_cols, grid_rows), use_sextants_(sextants) { }

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

    std::vector<term::SubcellRgb> samples(static_cast<size_t>(sub_rows * 2));
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

        term::SubcellRgb on_color, off_color;
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
        term::SubcellRgb sum{0, 0, 0};
        for (int qy = 0; qy < sub_rows; qy++) {
          int vy_from_top = label.row * sub_rows + qy;
          int vy = vrows - 1 - vy_from_top;
          for (int qx = 0; qx < 2; qx++) {
            int vx = sx * 2 + qx;
            size_t idx = static_cast<size_t>(vy * vcols + vx);
            term::SubcellRgb rgb = heatmapCellColor(background, agg_saturation[idx], agg_brightness[idx]);
            sum.r += rgb.r; sum.g += rgb.g; sum.b += rgb.b;
          }
        }
        float sample_count = static_cast<float>(sub_rows * 2);
        term::SubcellRgb mean{sum.r / sample_count, sum.g / sample_count, sum.b / sample_count};

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

void drawCellSparkline(Plane & plane, int row, int col, int rows, const SparklineReading & reading, const SparklineLook & look, int clip_top,
                     int clip_bottom) {
  if (rows <= 0) return;
  auto & styles = plane.getStyles();
  auto cells = glyph_meter::verticalBar(reading.fraction, rows, reading.peak);
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

void CellSpectrumMeter::drawBars(const std::vector<float> & levels) {
  auto [rows, cols] = getDim();
  auto & styles = getPlane().getStyles();
  setBgColor(styles.window_bg_color);
  fill();
  for (int c = 0; c < cols; c++) {
    auto column = [&](size_t bin) { return glyph_meter::BarColumn{glyph_meter::barSteps(levels[bin], rows), 0}; };
    auto bar = glyph_meter::verticalBar(glyph_meter::Glyphs::BRAILLE, rows, column(static_cast<size_t>(2 * c)), column(static_cast<size_t>(2 * c + 1)));
    for (int i = 0; i < rows; i++) {
      // Shaded by the height of this cell; i = 0 is the top one.
      setFgColor(styles.spectrumColor((static_cast<float>(rows - i) - 0.5f) / static_cast<float>(rows)));
      putstr(i, c, bar[static_cast<size_t>(i)]);
    }
  }
}

void CellChannelMeter::drawBars(int bar_rows, int cells) {
  auto & styles = getPlane().getStyles();
  for (int c = 0; c < cells; c++) {
    auto & left = channels_[static_cast<size_t>(2 * c)];
    auto & right = channels_[static_cast<size_t>(2 * c + 1)];
    auto column = [&](const Channel & channel) {
      return glyph_meter::BarColumn{glyph_meter::barSteps(channel.fraction, bar_rows), glyph_meter::barSteps(channel.peak_fraction, bar_rows)};
    };
    auto bar = glyph_meter::verticalBar(glyph_meter::Glyphs::BRAILLE, bar_rows, column(left), column(right));
    bool clipping = left.clipping || right.clipping;
    for (int i = 0; i < bar_rows; i++) {
      // Shaded by the height of this cell; i = 0 is the top one.
      setFgColor(clipping ? styles.meter_clip_color : styles.meterColor((static_cast<float>(bar_rows - i) - 0.5f) / static_cast<float>(bar_rows)));
      putstr(i, c, bar[static_cast<size_t>(i)]);
    }
  }
}

// Each sub-row of a cell gets its own centred, symmetric fill across the
// whole row's width, antialiased from the amplitudes' real values: a bar's
// edge is a fractional position, so the half-column slot it lands in is
// shaded by its overlap, and each sub-row averages the coverage of the
// slices that fall in it. A cell shows only two colours, so the best-fitting
// pair for its blended sub-samples picks the glyph.
void drawCellWaveformRow(Plane & plane, int row, int col, int width, const std::vector<float> & slices, const WaveformLook & look,
                         bool sextants) {
  if (width <= 0 || slices.empty()) return;
  const int sub_rows = sextants ? 3 : 2;
  const int per_sub_row = std::max<int>(1, static_cast<int>(slices.size()) / sub_rows);
  auto bg8 = look.bg.toSrgb8(), fg8 = look.fg.toSrgb8();
  term::SubcellRgb bg_rgb{static_cast<float>(bg8[0]), static_cast<float>(bg8[1]), static_cast<float>(bg8[2])};
  term::SubcellRgb fg_rgb{static_cast<float>(fg8[0]), static_cast<float>(fg8[1]), static_cast<float>(fg8[2])};
  const int total_slots = width * 2;
  auto slotCoverage = [&](float value, int gc) {
    value = std::clamp(value, 0.0f, 1.0f);
    double fill_count = static_cast<double>(value) * total_slots;
    double fill_start = (total_slots - fill_count) / 2.0;
    double fill_end = fill_start + fill_count;
    double overlap = std::min(static_cast<double>(gc + 1), fill_end) - std::max(static_cast<double>(gc), fill_start);
    return std::clamp(overlap, 0.0, 1.0);
  };
  std::vector<term::SubcellRgb> samples(static_cast<size_t>(sub_rows * 2));
  for (int c = 0; c < width; c++) {
    float max_coverage = 0.0f;
    for (int s = 0; s < sub_rows; s++) {
      for (int h = 0; h < 2; h++) {
        int gc = c * 2 + h;
        double coverage_sum = 0.0;
        for (int m = 0; m < per_sub_row; m++) {
          size_t index = std::min(slices.size() - 1, static_cast<size_t>(s * per_sub_row + m));
          coverage_sum += slotCoverage(slices[index], gc);
        }
        auto coverage = static_cast<float>(coverage_sum / per_sub_row);
        max_coverage = std::max(max_coverage, coverage);
        samples[static_cast<size_t>(s * 2 + h)] = term::SubcellRgb{
          bg_rgb.r * (1.0f - coverage) + fg_rgb.r * coverage,
          bg_rgb.g * (1.0f - coverage) + fg_rgb.g * coverage,
          bg_rgb.b * (1.0f - coverage) + fg_rgb.b * coverage,
        };
      }
    }
    // No coverage anywhere in the cell: checked against the coverage itself,
    // not the quantizer's mask, which ties at 0 for a fully covered cell too.
    constexpr float kQuietCoverageThreshold = 0.05f;
    if (max_coverage < kQuietCoverageThreshold) {
      // Guide lines at the centre (zero amplitude) and the quarter columns
      // (half amplitude, about -6 dBFS), only where nothing real is drawn.
      if (c == width / 2) {
        plane.setFgColor(look.guide);
        plane.setBgColor(look.bg);
        plane.putstr(row, col + c, "│");
        continue;
      }
      if (c == width / 4 || c == 3 * width / 4) {
        // Secondary to the centre line: half its strength, tinted with the clip's colour.
        auto tinted = look.guide.blend(0.5f, look.fg);
        plane.setFgColor(look.bg.blend(0.5f, tinted));
        plane.setBgColor(look.bg);
        plane.putstr(row, col + c, "│");
        continue;
      }
      // A faint dot tinted toward the clip's colour, so a quiet stretch still
      // reads as "the clip is here".
      constexpr float kQuietDotTint = 0.35f;
      auto darkened = look.fg.blend(0.5f, Color());
      plane.setFgColor(look.bg.blend(kQuietDotTint, darkened));
      plane.setBgColor(look.bg);
      plane.putstr(row, col + c, "·");
      continue;
    }
    term::SubcellRgb on_color, off_color;
    int mask = quantizeToTwoColors(samples, on_color, off_color);
    plane.setFgColor(Color(static_cast<int>(on_color.r), static_cast<int>(on_color.g), static_cast<int>(on_color.b)));
    plane.setBgColor(Color(static_cast<int>(off_color.r), static_cast<int>(off_color.g), static_cast<int>(off_color.b)));
    unsigned codepoint = sextants ? sextantCodepoint(mask) : kQuadrantCodepoints[mask];
    plane.putstr(row, col + c, utf8::encodeCodepoint(codepoint));
  }
}

namespace {

class CellCharts : public Charts {
 public:
  explicit CellCharts(bool sextants) : sextants_(sextants) { }

  std::shared_ptr<SpectrumMeter> spectrum(Plane & parent) const override { return std::make_shared<CellSpectrumMeter>(parent); }
  std::shared_ptr<HeatmapChart> heatmap(Plane & parent, int grid_cols, int grid_rows) const override {
    return std::make_shared<CellHeatmapChart>(parent, grid_cols, grid_rows, sextants_);
  }
  std::shared_ptr<ChannelMeter> channelMeter(Plane & parent) const override { return std::make_shared<CellChannelMeter>(parent); }
  void drawSparkline(Plane & plane, int row, int col, int rows, const SparklineReading & reading, const SparklineLook & look, int clip_top,
                   int clip_bottom, uint64_t) const override {
    drawCellSparkline(plane, row, col, rows, reading, look, clip_top, clip_bottom);
  }
  void drawWaveformRow(Plane & plane, int row, int col, int width, const std::vector<float> & slices, const WaveformLook & look) const override {
    drawCellWaveformRow(plane, row, col, width, slices, look, sextants_);
  }

 private:
  bool sextants_;
};

}  // namespace

}  // namespace term

std::unique_ptr<Charts> cellCharts(bool sextants) { return std::make_unique<term::CellCharts>(sextants); }

}  // namespace cellui
