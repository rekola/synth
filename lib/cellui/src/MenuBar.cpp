#include <cellui/MenuBar.h>

#include <cellui/Utf8.h>

#include <algorithm>

namespace cellui {

namespace {

// Columns of padding each side of an item's label in the open panel.
constexpr int kPad = 1;

int lower(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }

}  // namespace

void MenuBar::setSections(std::vector<Section> sections) {
  sections_ = std::move(sections);
  open_ = -1;
  selected_ = -1;
  draw();
}

int MenuBar::sectionColumn(int section) const {
  int x = 2;
  for (int i = 0; i < section && i < static_cast<int>(sections_.size()); i++) {
    x += utf8::displayWidth(sections_[static_cast<size_t>(i)].name) + 2;
  }
  return x;
}

int MenuBar::bodyWidth(int section) const {
  int w = 0;
  for (const Item & item : sections_[static_cast<size_t>(section)].items) w = std::max(w, utf8::displayWidth(item.label));
  return w;
}

int MenuBar::boxColumn() const {
  int x = sectionColumn(open_);
  int width = bodyWidth(open_) + 2 * kPad;
  if (x + width > cols_) x = std::max(0, cols_ - width);
  return x;
}

void MenuBar::layout(int parent_cols) {
  cols_ = parent_cols;
  size_t longest = 0;
  for (const Section & s : sections_) longest = std::max(longest, s.items.size());
  plane_.move(0, 0);
  plane_.resize(1 + static_cast<int>(longest) + 1, std::max(1, cols_));
  draw();
}

void MenuBar::draw() {
  plane_.erase();
  if (cols_ <= 0) return;
  auto ink = [&](const Color & fg, const Color & bg) {
    plane_.setFgColor(fg.getRed(), fg.getGreen(), fg.getBlue());
    plane_.setBgColor(bg.getRed(), bg.getGreen(), bg.getBlue());
  };

  // The header row, filled across, each name with its mnemonic marked.
  ink(colors_.header_fg, colors_.header_bg);
  plane_.setBold(false);
  plane_.putstr(0, 0, std::string(static_cast<size_t>(cols_), ' '));
  for (size_t i = 0; i < sections_.size(); i++) {
    const Section & s = sections_[i];
    bool any_enabled = std::any_of(s.items.begin(), s.items.end(), [](const Item & it) { return !it.separator && it.enabled; });
    ink(any_enabled ? colors_.header_fg : colors_.disabled_fg, colors_.header_bg);
    int x = sectionColumn(static_cast<int>(i));
    auto clusters = utf8::clusters(s.name);
    bool marked = false;
    for (const std::string & c : clusters) {
      bool mark = !marked && s.mnemonic && c.size() == 1 && lower(c[0]) == lower(s.mnemonic);
      if (mark) {
        plane_.setBold(true);
        plane_.setUnderline(true);
        marked = true;
      }
      plane_.putstr(0, x, c);
      if (mark) {
        plane_.setBold(false);
        plane_.setUnderline(false);
      }
      x += utf8::displayWidth(c);
    }
  }
  if (open_ < 0) return;

  // The open section: an opaque panel under the header, no border, a column
  // of padding each side of the labels and a row below them.
  const Section & s = sections_[static_cast<size_t>(open_)];
  int body = bodyWidth(open_);
  int width = body + 2 * kPad;
  int x0 = boxColumn();
  int rows = static_cast<int>(s.items.size());
  ink(colors_.section_fg, colors_.section_bg);
  for (int y = 1; y <= rows + 1; y++) plane_.putstr(y, x0, std::string(static_cast<size_t>(width), ' '));
  for (int i = 0; i < rows; i++) {
    int y = 1 + i;
    const Item & item = s.items[static_cast<size_t>(i)];
    if (item.separator) {
      std::string rule;
      for (int k = 0; k < body; k++) rule += "─";
      ink(colors_.disabled_fg, colors_.section_bg);
      plane_.putstr(y, x0 + kPad, rule);
      continue;
    }
    std::string text = std::string(kPad, ' ') + utf8::padToWidth(item.label, body) + std::string(kPad, ' ');
    if (i == selected_) {
      // The highlighted item: the section's colours swapped, across the panel.
      ink(colors_.section_bg, colors_.section_fg);
    } else {
      ink(item.enabled ? colors_.section_fg : colors_.disabled_fg, colors_.section_bg);
    }
    plane_.putstr(y, x0, text);
  }
}

bool MenuBar::selectable(int section, int item) const {
  const auto & items = sections_[static_cast<size_t>(section)].items;
  return item >= 0 && item < static_cast<int>(items.size()) && !items[static_cast<size_t>(item)].separator &&
         items[static_cast<size_t>(item)].enabled;
}

void MenuBar::open(int section) {
  if (section < 0 || section >= static_cast<int>(sections_.size())) return;
  open_ = section;
  selected_ = -1;
  int n = static_cast<int>(sections_[static_cast<size_t>(section)].items.size());
  for (int i = 0; i < n; i++) {
    if (selectable(section, i)) {
      selected_ = i;
      break;
    }
  }
  draw();
}

void MenuBar::close() {
  open_ = -1;
  selected_ = -1;
  draw();
}

void MenuBar::step(int delta) {
  int n = static_cast<int>(sections_[static_cast<size_t>(open_)].items.size());
  if (n == 0) return;
  int i = selected_ < 0 ? (delta > 0 ? -1 : 0) : selected_;
  for (int k = 0; k < n; k++) {
    i = ((i + delta) % n + n) % n;
    if (selectable(open_, i)) {
      selected_ = i;
      break;
    }
  }
  draw();
}

void MenuBar::stepSection(int delta) {
  int n = static_cast<int>(sections_.size());
  open(((open_ + delta) % n + n) % n);
}

std::optional<std::pair<int, int>> MenuBar::takeActivated() {
  auto a = activated_;
  activated_.reset();
  return a;
}

bool MenuBar::offerInput(const Input & input) {
  int id = input.getId();
  bool release = input.getKind() == Input::Kind::RELEASE;
  if (id == Key::Button1) {
    if (!release) return false;
    auto [py, px] = plane_.getPosition();
    int y = input.getY() - py, x = input.getX() - px;
    if (y == 0) {
      for (size_t i = 0; i < sections_.size(); i++) {
        int start = sectionColumn(static_cast<int>(i));
        if (x >= start && x < start + utf8::displayWidth(sections_[i].name)) {
          if (open_ == static_cast<int>(i)) close();
          else open(static_cast<int>(i));
          return true;
        }
      }
      if (open_ >= 0) close();
      return false;
    }
    if (open_ >= 0) {
      int item = y - 1, x0 = boxColumn();
      if (x >= x0 && x < x0 + bodyWidth(open_) + 2 * kPad && selectable(open_, item)) {
        activated_ = std::pair(open_, item);
        close();
        return true;
      }
    }
    return false;
  }
  if (release) return false;
  if (input.hasAlt() && !input.hasCtrl()) {
    for (size_t i = 0; i < sections_.size(); i++) {
      if (sections_[i].mnemonic && lower(sections_[i].mnemonic) == lower(id)) {
        open(static_cast<int>(i));
        return true;
      }
    }
  }
  if (open_ < 0) return false;
  if (id == Key::Left) stepSection(-1);
  else if (id == Key::Right) stepSection(1);
  else if (id == Key::Up || id == Key::Button4) step(-1);
  else if (id == Key::Down || id == Key::Button5) step(1);
  else if (id == Key::Escape) close();
  else if (id == Key::Enter) {
    if (selected_ >= 0) activated_ = std::pair(open_, selected_);
    close();
  } else {
    return false;
  }
  return true;
}

}  // namespace cellui
