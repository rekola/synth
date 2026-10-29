#include <cellui/ChoiceDialog.h>

#include <cellui/Input.h>
#include <cellui/Utf8.h>

#include <algorithm>
#include <cmath>

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
  fresh_ = true;
}

bool ChoiceDialog::animating() const {
  if (!isOpen()) return false;
  double target = list_.selected() - list_.offset();
  return list_.animating() || (list_.smooth() && cursor_y_ != target);
}

void ChoiceDialog::close() {
  plane_.reset();
  list_plane_.reset();
  labels_.clear();
  title_.clear();
}

void ChoiceDialog::show(Plane & parent, int screen_rows, int screen_cols) {
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

  if (!plane_) {
    plane_ = parent.createChild();
    plane_->setMouseCursor(MouseCursor::DEFAULT);
    plane_->setElevation(elevation_.value_or(plane_->getStyles().theme.elevation.overlay));
  }
  plane_->resize(rows_, width_);
  auto & styles = plane_->getStyles();

  auto bg = styles.picker_bg_color, fg = styles.window_fg_color, strong = styles.window_accent_fg_color;
  auto cursor_bg = styles.highlight_bg_color;
  auto set_on = [](Plane & plane, const Color & foreground, const Color & background) {
    plane.setFgColor(foreground);
    plane.setBgColor(background);
  };
  auto set = [&](const Color & foreground, const Color & background) { set_on(*plane_, foreground, background); };

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

  // The entries scroll smoothly where the display does, under a translucent
  // cursor band that eases to the selection.
  list_.setSmooth(parent.smoothScroll());
  int rows = visibleRows();
  list_.firstVisible(rows);
  double target = list_.selected() - list_.offset();
  if (fresh_) {
    list_.snap();
    cursor_y_ = target;
    fresh_ = false;
    last_tick_ = std::chrono::steady_clock::now();
  }
  auto now = std::chrono::steady_clock::now();
  double seconds = std::min(0.1, std::chrono::duration<double>(now - last_tick_).count());
  last_tick_ = now;
  list_.tick(seconds);
  if (!list_.smooth()) {
    cursor_y_ = target;
  } else {
    cursor_y_ += (target - cursor_y_) * (1.0 - std::exp(-seconds / ScrollAxis::kSettleSeconds));
    if (std::abs(target - cursor_y_) < ScrollAxis::kSnap) cursor_y_ = target;
  }
  cursor_y_ = std::clamp(cursor_y_, 0.0, static_cast<double>(std::max(0, rows - 1)));

  int count = static_cast<int>(labels_.size());
  int first = list_.offset();
  // The arrows ride the right margin of the box, beside the list.
  for (int i = 0; i < rows; i++) {
    const char * arrow = nullptr;
    if (i == 0 && first > 0) arrow = "▲";
    else if (i == rows - 1 && first + rows < count) arrow = "▼";
    plane_->putstr(kTopRows + i, width_ - 1, arrow ? arrow : " ");
  }

  x_ = (screen_cols - width_) / 2;
  y_ = (screen_rows - rows_) / 2;
  plane_->move(y_, x_);
  plane_->moveToTop();

  if (!list_plane_) {
    list_plane_ = parent.createChild();
    list_plane_->setMouseCursor(MouseCursor::POINTER);
    // Over the box, at its height.
    list_plane_->setElevation(plane_->elevation());
  }
  list_plane_->move(y_ + kTopRows, x_);
  list_plane_->resize(rows, width_ - 1);
  list_plane_->setScrollOffset(list_.fraction(), 0);
  list_plane_->erase();
  set_on(*list_plane_, fg, bg);
  int drawn = rows + (list_.smooth() ? 1 : 0);
  for (int i = 0; i < drawn; i++) {
    int index = list_.firstShown() + i;
    set_on(*list_plane_, fg, bg);
    string text;
    if (index < count) {
      // A marker for the one in use, then the label, padded so the cursor
      // covers the whole row.
      text = string(index == current_ ? "●" : " ") + " " + labels_[static_cast<size_t>(index)];
      // A label that doesn't fit ends in an ellipsis, so it is clear it goes on.
      int room = inner - 2;
      if (utf8::displayWidth(text) > room) text = utf8::truncateToWidth(text, room - 1) + "\u2026";
    }
    list_plane_->putstr(i, 0, " " + utf8::padToWidth(text, inner));
  }
  list_plane_->setBand(Band{ cursor_y_, 1.0, 0.0, static_cast<double>(width_ - 1), cursor_bg, 0.45f });
  list_plane_->moveToTop();
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
    list_.select(list_.indexAt(row));
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
