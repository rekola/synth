#include <cellui/Charts.h>

#include "HeatmapGrid.h"

#include <cellui/HeatmapImage.h>
#include <cellui/LineImage.h>
#include <cellui/StyleProvider.h>
#include <cellui/Plane.h>
#include <cellui/SparklineImage.h>
#include <cellui/WaveformImage.h>
#include <cellui/term/CellCharts.h>

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace cellui {

using namespace heatmap_grid;

namespace {

// The heatmap as the plane's image, drawn with canvas at the plane's size
// in pixels.
class PixelHeatmapChart : public HeatmapChart {
public:
  PixelHeatmapChart(Plane & parent, int grid_cols, int grid_rows)
    : HeatmapChart(parent, grid_cols, grid_rows) { }

  void setGrid(const std::vector<float> & brightness, const std::vector<float> & saturation) override {
    brightness_ = brightness;
    saturation_ = saturation;
  }

  void setMarkers(std::vector<Marker> markers) override {
    markers_ = std::move(markers);
  }

  void setLabels(std::vector<Label> labels) override {
    labels_ = std::move(labels);
  }

  void commit() override {
    if (brightness_.empty()) return;

    int px_h = 0, px_w = 0;
    if (!getPlane().pixelSize(px_h, px_w)) return;

    unsigned pxy = static_cast<unsigned>(px_h), pxx = static_cast<unsigned>(px_w);
    unsigned celldimy = std::max(1u, pxy / static_cast<unsigned>(std::max(1, getDim().first)));

    // Labels are drawn into the image itself: text in the plane's cells would
    // be hidden under the bitmap. Small, unobtrusive marks: a bit over half
    // the cell's height.
    drawPicture(getPlane(), heatmap_image::renderPicture(brightness_, saturation_, gridCols(), gridRows(), markers_, labels_, pxx, pxy,
                                                         getPlane().getStyles(), static_cast<float>(celldimy) * 0.6f, getPlane().hdrPictures()));
  }

private:
  std::vector<float> brightness_, saturation_;
  std::vector<Marker> markers_;
  std::vector<Label> labels_;
};

// The spectrum as the plane's image: one level per pixel column, drawn
// with canvas.
class PixelLineChart : public term::CellLineChart {
public:
  using term::CellLineChart::CellLineChart;

protected:
  size_t sampleCount(int cols) override {
    int pxy = 0, pxx = 0;
    if (getPlane().pixelSize(pxy, pxx)) return static_cast<size_t>(pxx);
    return term::CellLineChart::sampleCount(cols);
  }

  void drawSamples(const std::vector<float> & samples) override {
    int pxy = 0, pxx = 0;
    if (!getPlane().pixelSize(pxy, pxx) || samples.size() != static_cast<size_t>(pxx)) {
      term::CellLineChart::drawSamples(samples);
      return;
    }
    // Pixels are made lazily: until a level is above zero, only the
    // background, in cells.
    if (!has_pixels_) {
      if (std::all_of(samples.begin(), samples.end(), [](float v) { return v <= 0.0f; })) {
        setBgColor(getPlane().getStyles().window_bg_color);
        fill();
        return;
      }
      has_pixels_ = true;
    }
    // Opaque, so a pixel never composites against the raw terminal background.
    drawPicture(getPlane(), line_image::renderPicture(samples, static_cast<unsigned>(pxx), static_cast<unsigned>(pxy), getPlane().getStyles(),
                                                      static_cast<float>(getPlane().displayScale()), getPlane().hdrPictures()));
  }

 private:
  bool has_pixels_ = false;
};

class PixelCharts : public Charts {
 public:
  std::shared_ptr<LineChart> lineChart(Plane & parent) const override { return std::make_shared<PixelLineChart>(parent); }
  std::shared_ptr<HeatmapChart> heatmap(Plane & parent, int grid_cols, int grid_rows) const override {
    return std::make_shared<PixelHeatmapChart>(parent, grid_cols, grid_rows);
  }
  void drawSparkline(Plane & plane, int row, int col, int rows, const std::vector<SparklineReading> & readings, const SparklineLook & look,
                     int clip_top, int clip_bottom, uint64_t id) const override {
    int cell_h = 0, cell_w = 0;
    int top = std::max(row, clip_top), bottom = std::min(row + rows, clip_bottom);
    if (rows <= 0 || top >= bottom || readings.empty()) return;
    if (!plane.cellPixelSize(cell_h, cell_w)) {
      term::drawCellSparkline(plane, row, col, rows, readings, look, clip_top, clip_bottom);
      return;
    }
    // Pixels are made lazily: a meter that has never sounded is only its
    // background in text cells. Once it has pixels it keeps them, even at
    // zero.
    auto key = std::make_pair(&plane, id);
    if (!drawn_.count(key)) {
      bool silent = std::all_of(readings.begin(), readings.end(), [](const SparklineReading & r) { return r.fraction <= 0.0f && r.peak <= 0.0f; });
      if (silent) {
        term::drawCellSparkline(plane, row, col, rows, readings, look, clip_top, clip_bottom);
        return;
      }
      drawn_.insert(key);
    }
    // The whole meter, wherever it is scrolled to; the plane shows the rows
    // from clip_top to clip_bottom.
    // A lone bar in a cell's right slot, otherwise two to a cell.
    std::vector<std::optional<SparklineReading>> slots;
    if (readings.size() == 1) slots = {std::nullopt, readings[0]};
    else slots.assign(readings.begin(), readings.end());
    if (slots.size() % 2) slots.push_back(std::nullopt);
    unsigned width = static_cast<unsigned>(slots.size() / 2 * static_cast<size_t>(cell_w)), height = static_cast<unsigned>(rows * cell_h);
    drawPictureAt(plane, row, col,
                  sparkline_image::renderPicture(slots, look, width, height, plane.getStyles(), static_cast<float>(plane.displayScale()), plane.hdrPictures()), id,
                  clip_top, clip_bottom);
  }
  void drawWaveformRow(Plane & plane, int row, int col, int width, const std::vector<float> & slices, const WaveformLook & look) const override {
    int cell_h = 0, cell_w = 0;
    if (width <= 0 || !plane.cellPixelSize(cell_h, cell_w)) {
      term::drawCellWaveformRow(plane, row, col, width, slices, look, false);
      return;
    }
    unsigned pixel_w = static_cast<unsigned>(width * cell_w), pixel_h = static_cast<unsigned>(cell_h);
    drawPictureAt(plane, row, col,
                  waveform_image::renderPicture(slices, look.fg, look.bg, look.guide, pixel_w, pixel_h, static_cast<float>(plane.displayScale()), plane.hdrPictures()));
  }

 private:
  // The meters (plane, id) that have been drawn as pixels.
  mutable std::set<std::pair<const Plane *, uint64_t>> drawn_;
};

}  // namespace

std::unique_ptr<Charts> pixelCharts() { return std::make_unique<PixelCharts>(); }

}  // namespace cellui
