#include <cellui/InfoDialog.h>

#include <cellui/Markdown.h>
#include <cellui/Utf8.h>

#include <algorithm>

using namespace std;

namespace cellui {


void
InfoDialog::show(UIPlane & parent, const string & title, const string & source, int width, int max_rows) {
  auto lines = markdown::layout(markdown::parse(source), width - 4);
  // A padding row, the title, a gap, the text and a padding row.
  constexpr int kTopRows = 3, kChrome = 4;
  auto rows = std::min(static_cast<int>(lines.size()) + kChrome, std::max(kChrome + 1, max_rows));
  if (!plane_) {
    plane_ = parent.createChild();
    plane_->setMouseCursor(MouseCursor::DEFAULT);
    plane_->setElevation(elevation_.value_or(plane_->getStyles().theme.elevation.overlay));
  }
  auto & styles = plane_->getStyles();
  rows_ = rows;
  width_ = width;
  plane_->resize(rows, width);

  auto bg = styles.picker_bg_color, fg = styles.window_fg_color, strong = styles.window_accent_fg_color;
  plane_->setBgColor(bg.getRed(), bg.getGreen(), bg.getBlue());
  plane_->setFgColor(fg.getRed(), fg.getGreen(), fg.getBlue());
  plane_->setBold(false);
  plane_->setItalic(false);
  for (int row = 0; row < rows; row++) plane_->putstr(row, 0, string(static_cast<size_t>(width), ' '));
  plane_->setFgColor(strong.getRed(), strong.getGreen(), strong.getBlue());
  plane_->setBold(true);
  plane_->putstr(1, 2, utf8::truncateToWidth(title, width - 4));

  plane_->setFgColor(fg.getRed(), fg.getGreen(), fg.getBlue());
  for (int i = 0; i < rows - kChrome; i++) {
    int x = 2;
    for (auto & run : lines[static_cast<size_t>(i)]) {
      // The plane keeps one style at a time (bold wins over italic).
      plane_->setBold(run.bold);
      plane_->setItalic(!run.bold && run.italic);
      plane_->putstr(kTopRows + i, x, run.text);
      x += utf8::displayWidth(run.text);
    }
  }
  plane_->setBold(false);
  plane_->setItalic(false);
}

void
InfoDialog::move(int y, int x) {
  if (!plane_) return;
  plane_->move(y, x);
  plane_->moveToTop();
}

}  // namespace cellui
