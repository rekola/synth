#include <cellui/PickerPopup.h>

#include <cellui/Input.h>
#include <cellui/Utf8.h>

#include <algorithm>

namespace cellui {

using namespace std;

void PickerPopup::open(Plane & parent, int y, int x, int rows, int cols, vector<string> items, int selected) {
  if (plane_) return;
  items_ = std::move(items);
  selected_ = items_.empty() ? 0 : std::clamp(selected, 0, static_cast<int>(items_.size()) - 1);
  first_ = 0;
  y_ = y;
  x_ = x;
  rows_ = std::max(1, rows);
  cols_ = std::max(1, cols);
  plane_ = parent.createChild();
  plane_->setElevation(plane_->getStyles().theme.elevation.overlay);
  plane_->setMouseCursor(MouseCursor::POINTER);
  plane_->move(y_, x_);
  plane_->resize(rows_, cols_);
  plane_->moveToTop();
  draw();
}

void PickerPopup::close() {
  plane_.reset();
  items_.clear();
}

const string & PickerPopup::selection() const {
  static const string none;
  return items_.empty() ? none : items_[static_cast<size_t>(selected_)];
}

void PickerPopup::draw() {
  auto & styles = plane_->getStyles();
  int count = static_cast<int>(items_.size());
  int shown = std::max(0, std::min(count, rows_ - 2));
  int longest = 0;
  for (auto & item : items_) longest = std::max(longest, utf8::displayWidth(item));
  plane_->erase();
  plane_->setBold(false);
  plane_->setFgColor(styles.picker_fg_color);
  plane_->setBgColor(styles.picker_bg_color);
  for (int r = 0; r < rows_; r++) plane_->putstr(r, 0, string(static_cast<size_t>(cols_), ' '));
  if (count == 0 || shown == 0) return;
  // Keep the selection inside the window, which wraps around.
  int offset = ((selected_ - first_) % count + count) % count;
  if (offset >= shown) first_ = ((selected_ - shown + 1) % count + count) % count;
  for (int i = 0; i < shown; i++) {
    int item = (first_ + i) % count;
    bool selected = item == selected_;
    const string & text = items_[static_cast<size_t>(item)];
    int col = 2 + longest - utf8::displayWidth(text);
    plane_->setFgColor(selected ? styles.highlight_fg_color : styles.highlight_bg_color);
    plane_->setBgColor(selected ? styles.highlight_bg_color : styles.highlight_fg_color);
    plane_->putstr(1 + i, col, text);
  }
  if (shown < count) {
    plane_->setFgColor(styles.highlight_bg_color);
    plane_->setBgColor(styles.picker_bg_color);
    plane_->putstr(0, 2 + longest, "↑");
    plane_->putstr(rows_ - 1, 2 + longest, "↓");
  }
}

void PickerPopup::move(int delta) {
  int count = static_cast<int>(items_.size());
  if (count == 0) return;
  selected_ = ((selected_ + delta) % count + count) % count;
  draw();
}

PickerPopup::Result PickerPopup::key(int key, bool ctrl) {
  if (!plane_) return Result::IGNORED;
  int page = std::max(1, rows_ - 2);
  if (key == Key::Enter) return Result::CHOSEN;
  if (ctrl && key == 'g') return Result::CANCELLED;
  if (key == Key::Up) move(-1);
  else if (key == Key::Down) move(1);
  else if (key == Key::PageUp) move(-page);
  else if (key == Key::PageDown) move(page);
  else return Result::IGNORED;
  return Result::MOVED;
}

PickerPopup::Result PickerPopup::wheel(int direction) {
  if (!plane_) return Result::IGNORED;
  move(direction < 0 ? -1 : 1);
  return Result::MOVED;
}

PickerPopup::Result PickerPopup::click(int y, int x) {
  if (!plane_) return Result::IGNORED;
  int count = static_cast<int>(items_.size());
  int row = y - y_ - 1, col = x - x_;
  int shown = std::max(1, rows_ - 2);
  if (count == 0 || row < 0 || row >= shown || col < 0 || col >= cols_) return Result::CANCELLED;
  selected_ = (first_ + row) % count;
  draw();
  return Result::CHOSEN;
}

}  // namespace cellui
