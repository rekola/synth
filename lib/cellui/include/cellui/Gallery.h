#pragma once

#include <cellui/ChoiceDialog.h>
#include <cellui/Dial.h>
#include <cellui/InfoDialog.h>
#include <cellui/LineChart.h>
#include <cellui/SpinBox.h>
#include <cellui/Theme.h>
#include <cellui/Plane.h>

#include <memory>
#include <vector>

namespace cellui {

class Charts;

// The gallery page, drawn onto `plane` when constructed: every text style,
// the theme's colour tokens, the sub-cell glyph tables, the level meter ramp,
// a Markdown document, wide and combining characters, a choice list, an info
// dialog, a choice dialog, and the meters, spin box and dial widgets, as
// the terminal shows them. The page keeps the widgets and their planes, so it
// must outlive the refresh() that shows them. It draws through Plane
// only, so any backend can show it.
class Gallery {
 public:
  // The charts are the cell versions unless `charts` says otherwise (a
  // display that shows pictures passes chartsFor()). A picture needs the
  // root plane to know the cell size (BufferPlane::setCellPixelSize()).
  Gallery(Plane & plane, const Theme & theme);
  Gallery(Plane & plane, const Theme & theme, const Charts & charts);

  // The number of rows the page uses.
  int rows() const { return rows_; }

 private:
  InfoDialog dialog_;
  ChoiceDialog choice_;
  int octave_ = 4;
  std::shared_ptr<LineChart> spectrum_;
  std::unique_ptr<SpinBox> spin_;
  std::vector<std::unique_ptr<Dial>> dials_;
  int rows_ = 0;
};

}  // namespace cellui
