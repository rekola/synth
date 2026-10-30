#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/CoverArt.h>
#include <cellui/LineChart.h>
#include <cellui/term/CellCharts.h>

using namespace cellui;

TEST(linechart_redraws_only_when_the_curve_changes) {
  StyleProvider styles;
  CellBuffer b(4, 30);
  BufferPlane root(b, styles);
  term::CellLineChart meter(root);
  meter.resize(4, 30);
  std::vector<float> values(60, 0.4f);
  CHECK(meter.setValues(values));
  CHECK(!meter.setValues(values));
  values[20] = 0.9f;
  CHECK(meter.setValues(values));
  CHECK(!meter.setValues({}));
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
