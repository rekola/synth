#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/ChannelMeter.h>
#include <cellui/CoverArt.h>
#include <cellui/SpectrumMeter.h>

using namespace cellui;

TEST(channelmeter_draws_bars_and_a_legend_then_falls_to_rest) {
  StyleProvider styles;
  CellBuffer b(5, 10);
  BufferPlane root(b, styles);
  ChannelMeter meter(root);
  meter.resize(5, 10);
  CHECK(meter.atRest());
  meter.setLevels({1.0f, 0.5f, 0.0f, 0.25f}, "WYZX");
  root.refresh();
  CHECK(!meter.atRest());
  std::string text = b.toPlainText();
  CHECK(text.find("WYZX") != std::string::npos);
  CHECK(text.find("\xe2\xa3\xbf") != std::string::npos);  // a full braille cell: the loud channel's bar
  meter.clear("");
  CHECK(meter.atRest());
}

TEST(channelmeter_ignores_channels_beyond_its_room) {
  StyleProvider styles;
  CellBuffer b(4, 40);
  BufferPlane root(b, styles);
  ChannelMeter meter(root);
  meter.resize(4, 40);
  meter.setLevels(std::vector<float>(40, 0.5f), "x");  // more than kMaxChannels
  CHECK(!meter.atRest());
}

TEST(spectrummeter_redraws_only_when_the_curve_changes) {
  StyleProvider styles;
  CellBuffer b(4, 30);
  BufferPlane root(b, styles);
  SpectrumMeter meter(root);
  meter.resize(4, 30);
  std::vector<float> db(128, -30.0f);
  CHECK(meter.setSpectrum(db, 100.0f));
  CHECK(!meter.setSpectrum(db, 100.0f));
  db[40] = -10.0f;
  CHECK(meter.setSpectrum(db, 100.0f));
  root.refresh();
  CHECK(!b.toPlainText().empty());
  meter.clear();
  root.refresh();
  CHECK(b.toPlainText().empty());
}

TEST(coverart_shows_the_placeholder_once_and_is_twice_as_wide_as_tall) {
  StyleProvider styles;
  CellBuffer b(4, 10);
  BufferPlane root(b, styles);
  CoverArt art(root);
  CHECK(CoverArt::widthForHeight(4) == 8);
  art.resize(4, CoverArt::widthForHeight(4));
  CHECK(art.render(styles, false));
  CHECK(!art.render(styles, false));
  root.refresh();
  CHECK(b.toPlainText().find("\xf0\x9f\x96\xbc") != std::string::npos);  // the framed-picture glyph
}
