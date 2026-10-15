#include <cellui/ChoiceDialog.h>

#include <cellui/Input.h>
#include <cellui/Utf8.h>

#include <algorithm>

namespace cellui {

using namespace std;

namespace {

// Wide enough for a long device name plus where it is plugged in, which is
// what tells two of the same make apart.
constexpr int kMaxWidth = 110;
constexpr const char * kHint = "Enter: choose  Esc: cancel";
// The rows above the list (padding, title) and in all around it (also a gap,
// the hint and padding below).
constexpr int kTopRows = 2;
constexpr int kChromeRows = 5;

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

  // One column of padding each side of the text, and a column beside the
  // longest label for the scroll arrows.
  int widest = utf8::displayWidth(kHint) + 4;
  widest = std::max(widest, utf8::displayWidth(title_) + 4);
  for (auto & label : labels_) widest = std::max(widest, utf8::displayWidth(label) + 8);
  width_ = std::min({widest, kMaxWidth, std::max(10, screen_cols - 2)});
  // A padding row, the title, the list, a gap, the hint and a padding row,
  // leaving a little of the screen around it.
  rows_ = std::min(static_cast<int>(labels_.size()) + kChromeRows, std::max(kChromeRows + 1, screen_rows - 2));

  if (!plane_) plane_ = parent.createChild();
  plane_->resize(rows_, width_);
  auto & styles = plane_->getStyles();

  auto bg = styles.picker_bg_color, fg = styles.window_fg_color, strong = styles.window_accent_fg_color;
  auto cursor_fg = styles.highlight_fg_color, cursor_bg = styles.highlight_bg_color;
  auto set = [&](const Color & foreground, const Color & background) {
    plane_->setFgColor(foreground.getRed(), foreground.getGreen(), foreground.getBlue());
    plane_->setBgColor(background.getRed(), background.getGreen(), background.getBlue());
  };

  set(fg, bg);
  plane_->setBold(false);
  plane_->setItalic(false);
  for (int row = 0; row < rows_; row++) plane_->putstr(row, 0, string(static_cast<size_t>(width_), ' '));
  int inner = width_ - 2;
  set(strong, bg);
  plane_->setBold(true);
  plane_->putstr(1, 1, utf8::truncateToWidth(title_, inner));
  plane_->setBold(false);
  set(fg, bg);
  plane_->putstr(rows_ - 2, 1, utf8::truncateToWidth(kHint, inner));

  int first = list_.firstVisible(visibleRows());
  for (int i = 0; i < visibleRows(); i++) {
    int index = first + i;
    bool selected = index == list_.selected();
    if (selected) set(cursor_fg, cursor_bg);
    else set(fg, bg);
    // A marker for the one in use, then the label, padded so the cursor
    // covers the whole row; the last column is left for the scroll arrows.
    string text = string(index == current_ ? "●" : " ") + " " + labels_[static_cast<size_t>(index)];
    // A label that doesn't fit ends in an ellipsis, so it is clear it goes on.
    int room = inner - 2;
    if (utf8::displayWidth(text) > room) text = utf8::truncateToWidth(text, room - 1) + "\u2026";
    plane_->putstr(kTopRows + i, 0, " " + utf8::padToWidth(text, inner));
    plane_->putstr(kTopRows + i, width_ - 1, " ");
    // The arrows ride the right margin.
    const char * arrow = nullptr;
    if (i == 0 && first > 0) arrow = "▲";
    else if (i == visibleRows() - 1 && first + visibleRows() < static_cast<int>(labels_.size())) arrow = "▼";
    if (arrow) {
      set(fg, bg);
      plane_->putstr(kTopRows + i, width_ - 1, arrow);
    }
  }

  x_ = (screen_cols - width_) / 2;
  y_ = (screen_rows - rows_) / 2;
  plane_->move(y_, x_);
  plane_->moveToTop();
}

ChoiceDialog::Result
ChoiceDialog::offerInput(const cellui::Input & input) {
  if (labels_.empty()) return Result::CANCELLED;
  auto id = input.getId();

  if (id == Key::Button1) {
    // Resolved on release, like every other click in the app.
    if (input.getKind() != Input::Kind::RELEASE) return Result::NONE;
    int row = input.getY() - y_ - kTopRows, col = input.getX() - x_;
    if (row < 0 || row >= visibleRows() || col < 0 || col >= width_) {
      // Outside the list: the padding and title are inert, the page behind dismisses.
      bool inside_box = input.getY() >= y_ && input.getY() < y_ + rows_ && col >= 0 && col < width_;
      return inside_box ? Result::NONE : Result::CANCELLED;
    }
    list_.select(list_.firstVisible(visibleRows()) + row);
    return Result::CHOSEN;
  }
  // Releases of keys do nothing; everything below acts on the press.
  if (input.getKind() == Input::Kind::RELEASE) return Result::NONE;

  if (id == Key::Enter) return Result::CHOSEN;
  if (id == Key::Escape || id == 'q' || (input.hasCtrl() && id == 'g')) return Result::CANCELLED;

  int page = std::max(1, visibleRows() - 1);
  if (id == Key::Up || (input.hasCtrl() && id == 'p'))
    list_.move(-1);
  else if (id == Key::Down || (input.hasCtrl() && id == 'n'))
    list_.move(1);
  else if (id == Key::PageUp)
    list_.move(-page);
  else if (id == Key::PageDown)
    list_.move(page);
  else if (id == Key::Home)
    list_.home();
  else if (id == Key::End)
    list_.end();
  else if (id == Key::Button4)
    list_.scroll(-1, visibleRows());
  else if (id == Key::Button5)
    list_.scroll(1, visibleRows());
  return Result::NONE;
}

}  // namespace cellui
