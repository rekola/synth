#include <cellui/InfoDialog.h>

#include <cellui/Markdown.h>
#include <cellui/Utf8.h>

#include <algorithm>

using namespace std;

namespace cellui {

namespace {

string repeatUtf8(const string & glyph, int count) {
  string out;
  for (int i = 0; i < count; i++) out += glyph;
  return out;
}

} // namespace

void
InfoDialog::show(UIPlane & parent, const string & title, const string & source, int width, int max_rows) {
  auto lines = markdown::layout(markdown::parse(source), width - 4);
  auto rows = std::min(static_cast<int>(lines.size()) + 2, std::max(3, max_rows));
  if (!plane_) plane_ = parent.createChild();
  rows_ = rows;
  width_ = width;
  plane_->resize(rows, width);

  auto & styles = plane_->getStyles();
  auto bg = styles.window_accent_bg_color, fg = styles.window_fg_color, border = styles.window_border_color;
  plane_->setBgColor(bg.getRed(), bg.getGreen(), bg.getBlue());
  plane_->setFgColor(border.getRed(), border.getGreen(), border.getBlue());
  plane_->setBold(false);
  auto heading = "┌─ " + title + " ";
  plane_->putstr(0, 0, heading + repeatUtf8("─", std::max(0, width - 1 - utf8::displayWidth(heading))) + "┐");
  for (int row = 1; row < rows - 1; row++) {
    plane_->putstr(row, 0, "│" + string(static_cast<size_t>(width - 2), ' ') + "│");
  }
  plane_->putstr(rows - 1, 0, "└" + repeatUtf8("─", width - 2) + "┘");

  plane_->setFgColor(fg.getRed(), fg.getGreen(), fg.getBlue());
  for (int i = 0; i < rows - 2; i++) {
    int x = 2;
    for (auto & run : lines[static_cast<size_t>(i)]) {
      // The plane keeps one style at a time (bold wins over italic); each
      // run sets both, so one doesn't carry into the next.
      plane_->setBold(run.bold);
      plane_->setItalic(!run.bold && run.italic);
      plane_->putstr(i + 1, x, run.text);
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
