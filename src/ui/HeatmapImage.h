#ifndef _HEATMAPIMAGE_H_
#define _HEATMAPIMAGE_H_

#include "HeatmapChart.h"
#include "StyleProvider.h"

#include <cstdint>
#include <vector>

namespace heatmap_image {

// The colour of one cell: the window background at value 0, the fully lit
// fixed-hue colour at value 1, a straight lerp between (so a value that only
// ever decays towards 0 settles on the background without a cutoff).
void cellColor(const Color & background, float saturation, float value, float & r, float & g, float & b);

// The heatmap as pixels: `brightness`/`saturation` are cols*rows entries,
// row 0 at the bottom (HeatmapChart::setGrid()), smoothly scaled up to
// width x height, a 3x3 white square on each marker (u, v in 0..1, v up).
// Returns width*height pixels, row 0 at the top, one uint32_t per pixel with
// bytes R, G, B, A in memory order (all opaque).
std::vector<uint32_t> render(const std::vector<float> & brightness, const std::vector<float> & saturation, int cols, int rows,
                             const std::vector<HeatmapChart::Marker> & markers, unsigned width, unsigned height, const StyleProvider & styles);

}  // namespace heatmap_image

#endif
