#pragma once

#include <cellui/ChannelMeter.h>
#include <cellui/DisplayInfo.h>
#include <cellui/HeatmapChart.h>
#include <cellui/SpectrumMeter.h>
#include <cellui/VuMeterImage.h>

#include <memory>

namespace cellui {

// The scopes - the spectrum, the DirAC heatmap and the VU meters - in the
// versions a backend can show. An application asks its backend's Scopes
// for a scope and never knows how it is drawn.
class Scopes {
 public:
  virtual ~Scopes() = default;

  virtual std::shared_ptr<SpectrumMeter> spectrum(UIPlane & parent) const = 0;
  // A heatmap of a grid_cols x grid_rows grid.
  virtual std::shared_ptr<HeatmapChart> heatmap(UIPlane & parent, int grid_cols, int grid_rows) const = 0;
  virtual std::shared_ptr<ChannelMeter> channelMeter(UIPlane & parent) const = 0;
  // A VU meter drawn into a widget's own plane: `rows` cells tall from
  // (row, col), one cell wide, drawn only in the plane rows from clip_top up
  // to clip_bottom (a meter partly scrolled out of view). `id` names the
  // meter for the plane's images (see UIPlane::drawImageAt()); 0 for none.
  virtual void drawVuMeter(UIPlane & plane, int row, int col, int rows, const VuReading & reading, const VuMeterLook & look,
                           int clip_top, int clip_bottom, uint64_t id = 0) const = 0;
};

// The cell version of drawVuMeter(): a braille bar in the cell's right dot
// column, shaded by height (or in look.color), on look.background.
void drawCellVuMeter(UIPlane & plane, int row, int col, int rows, const VuReading & reading, const VuMeterLook & look, int clip_top,
                     int clip_bottom);

// Drawn in cells by hand: braille bars, a quadrant or sextant heatmap, the
// labels in text. Every backend can show these.
std::unique_ptr<Scopes> cellScopes();

// Drawn as pixels with canvas, for a backend whose planes show images
// (UIPlane::pixelSize()/drawImage()). Not available where canvas has no
// pixel backend built (nullptr).
std::unique_ptr<Scopes> pixelScopes();

// The scopes a display shows: the pixel version when pictures can be shown,
// else the cell version.
inline std::unique_ptr<Scopes> scopesFor(const DisplayInfo & display) {
  if (display.pictures && display.cell_px_height > 0 && display.cell_px_width > 0) {
    if (auto pixel = pixelScopes()) return pixel;
  }
  return cellScopes();
}

}  // namespace cellui
