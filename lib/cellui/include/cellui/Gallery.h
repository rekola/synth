#pragma once

#include <cellui/InfoDialog.h>
#include <cellui/Theme.h>
#include <cellui/UIPlane.h>

namespace cellui {

// The gallery page, drawn onto `plane` when constructed: every text style,
// the theme's colour tokens, the sub-cell glyph tables, the level meter ramp,
// a Markdown document, wide and combining characters, a choice list and an
// info dialog, as the terminal shows them. The page keeps the dialog's plane,
// so it must outlive the refresh() that shows it. It draws through UIPlane
// only, so any backend can show it.
class Gallery {
 public:
  Gallery(UIPlane & plane, const Theme & theme);

  // The number of rows the page uses.
  int rows() const { return rows_; }

 private:
  InfoDialog dialog_;
  int rows_ = 0;
};

}  // namespace cellui
