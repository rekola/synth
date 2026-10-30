#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/Charts.h>

#include "../src/ui/ChannelMeter.h"

using namespace cellui;

TEST(channel_meter_draws_bars_and_a_legend_then_falls_to_rest) {
  StyleProvider styles;
  CellBuffer b(5, 20);
  BufferPlane root(b, styles);
  auto charts = cellCharts();
  ChannelMeter meter(root, *charts);
  meter.resize(5, 20);
  CHECK(meter.atRest());
  meter.setLevels({1.0f, 0.5f, 0.0f, 0.25f}, "WYZX");
  root.refresh();
  CHECK(!meter.atRest());
  std::string text = b.toPlainText();
  CHECK(text.find("WYZX") != std::string::npos);
  CHECK(text.find("\xe2\xa2\xb8") != std::string::npos); // a full right braille column: the loud channel's bar
  meter.clear("");
  CHECK(meter.atRest());
}

TEST(channel_meter_ignores_channels_beyond_its_room) {
  StyleProvider styles;
  CellBuffer b(4, 40);
  BufferPlane root(b, styles);
  auto charts = cellCharts();
  ChannelMeter meter(root, *charts);
  meter.resize(4, 40);
  meter.setLevels(std::vector<float>(40, 0.5f), "x"); // more than kMaxChannels
  CHECK(!meter.atRest());
}
