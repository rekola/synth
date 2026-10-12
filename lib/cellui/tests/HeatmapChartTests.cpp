#include "TestFramework.h"

#include <cellui/HeatmapChart.h>

using namespace cellui;

TEST(heatmap_hsv_primaries) {
  uint8_t r, g, b;
  heatmapHsvToRgb(0.0f, 1.0f, 1.0f, r, g, b);
  CHECK(r == 255 && g == 0 && b == 0);
  heatmapHsvToRgb(120.0f, 1.0f, 1.0f, r, g, b);
  CHECK(r == 0 && g == 255 && b == 0);
  heatmapHsvToRgb(240.0f, 1.0f, 1.0f, r, g, b);
  CHECK(r == 0 && g == 0 && b == 255);
}

TEST(heatmap_hsv_zero_saturation_is_grey) {
  uint8_t r, g, b;
  heatmapHsvToRgb(200.0f, 0.0f, 0.5f, r, g, b);
  CHECK(r == g && g == b);
  CHECK(r == 127);
}

TEST(heatmap_fixed_hue_is_the_command_column_orange) {
  uint8_t r, g, b;
  heatmapHsvToRgb(kHeatmapHue, 1.0f, 1.0f, r, g, b);
  CHECK(r == 255);
  CHECK(g > 130 && g < 150);
  CHECK(b == 0);
}
