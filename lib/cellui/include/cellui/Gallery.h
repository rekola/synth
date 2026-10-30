#pragma once

#include <cellui/LevelBars.h>
#include <cellui/ChoiceDialog.h>
#include <cellui/CoverArt.h>
#include <cellui/Dial.h>
#include <cellui/InfoDialog.h>
#include <cellui/SpectrumChart.h>
#include <cellui/SpinBox.h>
#include <cellui/Theme.h>
#include <cellui/Plane.h>

#include <memory>
#include <vector>

namespace cellui {

// The gallery page, drawn onto `plane` when constructed: every text style,
// the theme's colour tokens, the sub-cell glyph tables, the level meter ramp,
// a Markdown document, wide and combining characters, a choice list, an info
// dialog, a choice dialog, and the meters, spin box, cover art and dial widgets, as
// the terminal shows them. The page keeps the widgets and their planes, so it
// must outlive the refresh() that shows them. It draws through Plane
// only, so any backend can show it.
class Gallery {
 public:
  Gallery(Plane & plane, const Theme & theme);

  // The number of rows the page uses.
  int rows() const { return rows_; }

 private:
  InfoDialog dialog_;
  ChoiceDialog choice_;
  int octave_ = 4;
  std::shared_ptr<SpectrumChart> spectrum_;
  std::shared_ptr<LevelBars> channels_;
  std::unique_ptr<SpinBox> spin_;
  std::unique_ptr<CoverArt> cover_;
  std::vector<std::unique_ptr<Dial>> dials_;
  int rows_ = 0;
};

}  // namespace cellui
