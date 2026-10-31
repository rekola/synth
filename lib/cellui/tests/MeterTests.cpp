#include "TestFramework.h"

#include <cellui/BufferPlane.h>
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
