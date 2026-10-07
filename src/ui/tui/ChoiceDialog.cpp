#include "ChoiceDialog.h"

#include "../../playback/InputEvent.h"
#include "../../util/Utf8.h"

#include <notcurses/notcurses.h>

#include <algorithm>

using namespace std;

namespace {

// Wide enough for a long device name plus where it is plugged in, which is
// what tells two of the same make apart.
constexpr int kMaxWidth = 110;
constexpr const char * kHint = "Enter: choose  Esc: cancel";

string repeatUtf8(const string & glyph, int count) {
  string out;
  for (int i = 0; i < count; i++) out += glyph;
  return out;
}

} // namespace

void ChoiceDialog::open(string title, vector<string> labels, int current) {
  title_ = std::move(title);
  labels_ = std::move(labels);
  current_ = current;
  list_.reset(static_cast<int>(labels_.size()), std::max(0, current));
}

void ChoiceDialog::close() {
  plane_.reset();
  labels_.clear();
  title_.clear();
}

void ChoiceDialog::show(UIPlane & parent, int screen_rows, int screen_cols) {
  if (labels_.empty()) return;

  // Each border line is its text plus "x- " before and " " and the corner
  // after, so the footer and title need that much room beside the box edge.
  int widest = Utf8::displayWidth(kHint) + 6;
  widest = std::max(widest, Utf8::displayWidth(title_) + 6);
  for (auto & label : labels_) widest = std::max(widest, Utf8::displayWidth(label) + 6);
  width_ = std::min({widest, kMaxWidth, std::max(10, screen_cols - 2)});
  // Two rows for the border, and leave a little of the screen around it.
  rows_ = std::min(static_cast<int>(labels_.size()) + 2, std::max(3, screen_rows - 2));

  if (!plane_) plane_ = parent.createChild();
  plane_->resize(rows_, width_);

  auto & styles = plane_->getStyles();
  auto bg = styles.window_accent_bg_color, fg = styles.window_fg_color, border = styles.window_border_color;
  auto cursor_fg = styles.highlight_fg_color, cursor_bg = styles.highlight_bg_color;

  plane_->setBgColor(bg.getRed(), bg.getGreen(), bg.getBlue());
  plane_->setFgColor(border.getRed(), border.getGreen(), border.getBlue());
  plane_->setBold(false);
  auto heading = "┌─ " + title_ + " ";
  plane_->putstr(0, 0, heading + repeatUtf8("─", std::max(0, width_ - 1 - Utf8::displayWidth(heading))) + "┐");
  auto footing = string("└─ ") + kHint + " ";
  plane_->putstr(rows_ - 1, 0, footing + repeatUtf8("─", std::max(0, width_ - 1 - Utf8::displayWidth(footing))) + "┘");

  int first = list_.firstVisible(visibleRows());
  int inner = width_ - 2;
  for (int i = 0; i < visibleRows(); i++) {
    int index = first + i;
    bool selected = index == list_.selected();

    plane_->setBgColor(bg.getRed(), bg.getGreen(), bg.getBlue());
    plane_->setFgColor(border.getRed(), border.getGreen(), border.getBlue());
    // The border's right edge doubles as the scroll indicator.
    string edge = "│";
    if (i == 0 && first > 0)
      edge = "▲";
    else if (i == visibleRows() - 1 && first + visibleRows() < static_cast<int>(labels_.size()))
      edge = "▼";
    plane_->putstr(i + 1, 0, "│");
    plane_->putstr(i + 1, width_ - 1, edge);

    if (selected) {
      plane_->setBgColor(cursor_bg.getRed(), cursor_bg.getGreen(), cursor_bg.getBlue());
      plane_->setFgColor(cursor_fg.getRed(), cursor_fg.getGreen(), cursor_fg.getBlue());
    } else {
      plane_->setBgColor(bg.getRed(), bg.getGreen(), bg.getBlue());
      plane_->setFgColor(fg.getRed(), fg.getGreen(), fg.getBlue());
    }
    // A marker for the one in use, then the label, padded so the cursor
    // covers the whole row.
    string text = string(" ") + (index == current_ ? "●" : " ") + " " + labels_[static_cast<size_t>(index)];
    // A label that doesn't fit ends in an ellipsis, so it is clear it goes on.
    if (Utf8::displayWidth(text) > inner) text = Utf8::truncateToWidth(text, inner - 1) + "\u2026";
    plane_->putstr(i + 1, 1, Utf8::padToWidth(text, inner));
  }

  x_ = (screen_cols - width_) / 2;
  y_ = (screen_rows - rows_) / 2;
  plane_->move(y_, x_);
  plane_->moveToTop();
}

ChoiceDialog::Result
ChoiceDialog::offerInput(const InputEvent & input) {
  if (labels_.empty()) return Result::CANCELLED;
  auto id = input.getId();

  if (id == NCKEY_BUTTON1) {
    // Resolved on release, like every other click in the app.
    if (input.getKind() != InputEvent::Kind::RELEASE) return Result::NONE;
    int row = input.getY() - y_ - 1, col = input.getX() - x_;
    if (row < 0 || row >= visibleRows() || col < 1 || col >= width_ - 1) {
      // Outside the list (the border counts as outside): dismiss.
      bool inside_box = input.getY() >= y_ && input.getY() < y_ + rows_ && col >= 0 && col < width_;
      return inside_box ? Result::NONE : Result::CANCELLED;
    }
    list_.select(list_.firstVisible(visibleRows()) + row);
    return Result::CHOSEN;
  }
  // Releases of keys do nothing; everything below acts on the press.
  if (input.getKind() == InputEvent::Kind::RELEASE) return Result::NONE;

  if (id == NCKEY_ENTER) return Result::CHOSEN;
  if (id == NCKEY_ESC || id == 'q' || (input.hasCtrl() && id == 'g')) return Result::CANCELLED;

  int page = std::max(1, visibleRows() - 1);
  if (id == NCKEY_UP || (input.hasCtrl() && id == 'p'))
    list_.move(-1);
  else if (id == NCKEY_DOWN || (input.hasCtrl() && id == 'n'))
    list_.move(1);
  else if (id == NCKEY_PGUP)
    list_.move(-page);
  else if (id == NCKEY_PGDOWN)
    list_.move(page);
  else if (id == NCKEY_HOME)
    list_.home();
  else if (id == NCKEY_END)
    list_.end();
  else if (id == NCKEY_BUTTON4)
    list_.scroll(-1, visibleRows());
  else if (id == NCKEY_BUTTON5)
    list_.scroll(1, visibleRows());
  return Result::NONE;
}
