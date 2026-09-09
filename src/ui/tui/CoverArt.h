#ifndef _COVERART_H_
#define _COVERART_H_

#include "../UIElement.h"
#include "../StyleProvider.h"
#include "../../util/Utf8.h"

#include <algorithm>

// Always-visible square thumbnail in the screen's top-left corner,
// immediately left of ArrangementGrid in the scope row (see UI::layout()).
// Song carries no cover-art data at all yet (no image loading/storage
// anywhere in this codebase), so this always shows the same framed-
// picture placeholder glyph; once a real per-song image exists, render()
// is where it gets swapped in for the placeholder.
class CoverArt : public UIElement {
 public:
  explicit CoverArt(UIPlane & parent) : UIElement(parent) { }

  // Terminal cells are roughly twice as tall as they are wide, so a
  // `rows`-tall widget needs about `rows * 2` columns to read as square on
  // screen. A single shared calculation (not a hardcoded width at each call
  // site) so this widget and whatever column it displaces (UI::layout()'s
  // own chart_ width) can't drift apart.
  static int widthForHeight(int rows) { return rows * 2; }

  bool render(const StyleProvider & styles, bool refresh) {
    auto [rows, cols] = getDim();
    if (rows < 1 || cols < 1) return false;
    if (!refresh && rows == current_rows_ && cols == current_cols_) return false;
    current_rows_ = rows;
    current_cols_ = cols;

    // Plain window background, same as ArrangementGrid/InfoLine sit on - no
    // border of its own either: UI::layout() already draws a "│" divider
    // column immediately on either side of this widget, so a full box here
    // would just double up on those two sides for no benefit. erase() alone
    // only resets cells back to the plane's own default base cell (set once
    // at plane creation - see UIPlane::createChild()), not to whatever
    // fg/bg was just set above - only a cell actually written via putstr()
    // picks that up, which is why fill() (paints every cell blank in the
    // current style) is what's needed here, not erase().
    setFgColor(styles.window_fg_color);
    setBgColor(styles.window_bg_color);
    fill();

    // Framed-picture glyph (U+1F5BC) - a generic "no image" placeholder,
    // not a music note, so it reads as "cover art goes here" rather than
    // as actual decorative content.
    static constexpr const char * kPlaceholderGlyph = "\U0001F5BC";
    auto glyph_width = Utf8::displayWidth(kPlaceholderGlyph);
    auto y = rows / 2;
    auto x = std::max(0, (cols - glyph_width) / 2);
    putstr(y, x, kPlaceholderGlyph);

    return true;
  }

 private:
  // Dirty-check cache: nothing else about this widget's content varies yet
  // (see the class comment), so a resize is currently the only reason to
  // redraw - same convention SpinBox/InfoLine's own current_*_ fields use.
  int current_rows_ = -1, current_cols_ = -1;
};

#endif
