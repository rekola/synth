#include <cellui/CommandPalette.h>

#include <cellui/FuzzyMatch.h>
#include <cellui/Utf8.h>

#include <algorithm>
#include <cctype>

namespace cellui {

using namespace std;

namespace {

constexpr int kMaxListRows = 12;
constexpr const char * kHint = "Tab: complete  Esc: cancel";
// The description and the query row, below the list.
constexpr int kChromeRows = 2;

}  // namespace

void CommandPalette::open(vector<PaletteEntry> entries) {
  entries_ = std::move(entries);
  query_.clear();
  open_ = true;
  refilter();
}

void CommandPalette::close() {
  plane_.reset();
  entries_.clear();
  shown_.clear();
  query_.clear();
  open_ = false;
}

// Emacs-style completion: the query becomes the longest prefix shared by
// every name that starts with it. It never runs anything, and when there is
// nothing to add it only says why.
void CommandPalette::complete() {
  auto same = [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); };
  string common;
  int matches = 0;
  for (auto & entry : entries_) {
    auto & name = entry.name;
    if (name.size() < query_.size() || !equal(query_.begin(), query_.end(), name.begin(), same)) continue;
    if (matches++ == 0) {
      common = name;
      continue;
    }
    size_t n = 0;
    while (n < common.size() && n < name.size() && common[n] == name[n]) n++;
    common.resize(n);
  }
  if (matches == 0) {
    note_ = "[No match]";
  } else if (common.size() > query_.size()) {
    query_ = common;
    refilter();
  } else {
    note_ = matches == 1 ? "[Sole completion]" : "";
  }
}

void CommandPalette::refilter() {
  note_.clear();
  struct Scored {
    int score;
    size_t entry;
    vector<int> positions;
  };
  vector<Scored> scored;
  for (size_t i = 0; i < entries_.size(); i++) {
    if (auto m = fuzzyMatch(query_, entries_[i].name)) scored.push_back({m->score, i, std::move(m->positions)});
  }
  stable_sort(scored.begin(), scored.end(), [&](const Scored & a, const Scored & b) {
    if (a.score != b.score) return a.score > b.score;
    return entries_[a.entry].name < entries_[b.entry].name;
  });
  shown_.clear();
  for (auto & s : scored) shown_.push_back({s.entry, std::move(s.positions)});
  list_.reset(static_cast<int>(shown_.size()), 0);
}

const PaletteEntry * CommandPalette::chosen() const {
  if (shown_.empty()) return nullptr;
  return &entries_[shown_[static_cast<size_t>(list_.selected())].entry];
}

vector<string> CommandPalette::shownNames() const {
  vector<string> names;
  for (auto & s : shown_) names.push_back(entries_[s.entry].name);
  return names;
}

void CommandPalette::show(UIPlane & parent, int screen_rows, int screen_cols) {
  if (!open_) return;

  // As many list rows as there are matches (one for the "no match" note), up
  // to a limit, and always leaving a row of the screen above.
  int count = static_cast<int>(shown_.size());
  int list_rows = max(1, min({count, kMaxListRows, screen_rows - kChromeRows - 1}));
  width_ = max(20, screen_cols);
  rows_ = list_rows + kChromeRows;

  if (!plane_) {
    plane_ = parent.createChild();
    // Left alone, the two rows below the list show the display's own
    // background, like the status line they replace.
    plane_->makeTransparent();
  }
  plane_->resize(rows_, width_);
  auto & styles = plane_->getStyles();

  auto bg = styles.picker_bg_color, fg = styles.window_fg_color, strong = styles.window_accent_fg_color;
  auto quiet = styles.theme.text_secondary;
  auto cursor_fg = styles.highlight_fg_color, cursor_bg = styles.highlight_bg_color;
  auto set = [&](const Color & foreground, const Color & background) {
    plane_->setFgColor(foreground.getRed(), foreground.getGreen(), foreground.getBlue());
    plane_->setBgColor(background.getRed(), background.getGreen(), background.getBlue());
  };

  // Text on the display's own background, for the description and the query.
  auto plain = [&](const Color & foreground) {
    plane_->setBgDefault();
    plane_->setFgColor(foreground.getRed(), foreground.getGreen(), foreground.getBlue());
  };

  set(fg, bg);
  plane_->setBold(false);
  plane_->setItalic(false);
  plane_->setUnderline(false);
  // The list is a raised panel; the description and the query are not.
  for (int row = 0; row < list_rows; row++) plane_->putstr(row, 0, string(static_cast<size_t>(width_), ' '));
  plain(fg);
  for (int row = list_rows; row < rows_; row++) plane_->putstr(row, 0, string(static_cast<size_t>(width_), ' '));
  int inner = width_ - 2;

  int first = list_.firstVisible(visibleRows());
  if (count == 0) {
    set(quiet, bg);
    plane_->putstr(0, 1, "No matching command");
  }
  for (int i = 0; i < visibleRows() && first + i < count; i++) {
    int index = first + i;
    bool selected = index == list_.selected();
    auto & row = shown_[static_cast<size_t>(index)];
    auto & entry = entries_[row.entry];
    auto name_fg = selected ? cursor_fg : fg;
    auto row_bg = selected ? cursor_bg : bg;
    auto binding_fg = selected ? cursor_fg : quiet;
    set(name_fg, row_bg);
    plane_->putstr(i, 0, string(static_cast<size_t>(width_ - 1), ' '));
    int binding_width = utf8::displayWidth(entry.binding);
    int name_room = inner - 1 - (binding_width > 0 ? binding_width + 2 : 0);
    string name = utf8::truncateToWidth(entry.name, name_room);
    plane_->putstr(i, 1, name);
    // The characters the query matched.
    set(selected ? cursor_fg : strong, row_bg);
    plane_->setBold(true);
    for (int p : row.positions) {
      if (p < utf8::displayWidth(name)) plane_->putstr(i, 1 + p, string(1, entry.name[static_cast<size_t>(p)]));
    }
    plane_->setBold(false);
    if (binding_width > 0) {
      set(binding_fg, row_bg);
      plane_->putstr(i, width_ - 1 - binding_width - 1, entry.binding);
    }
    // The arrows ride the right margin.
    const char * arrow = nullptr;
    if (i == 0 && first > 0) arrow = "▲";
    else if (i == visibleRows() - 1 && first + visibleRows() < count) arrow = "▼";
    if (arrow) {
      set(quiet, bg);
      plane_->putstr(i, width_ - 1, arrow);
    }
  }

  // The selected command's description, or what the last Tab had to say.
  // Quiet and italic on the display's background, so it is not taken for a
  // command.
  plain(quiet);
  plane_->setItalic(true);
  if (!note_.empty()) plane_->putstr(rows_ - 2, 1, note_);
  else if (auto * entry = chosen()) plane_->putstr(rows_ - 2, 1, utf8::truncateToWidth(entry->description, inner));
  plane_->setItalic(false);

  // The query on the last row, with a block where the next character goes
  // and the keys that matter at the right when there is room.
  string prompt = plane_->keymapProfile() == KeymapProfile::EMACS ? "M-x " : "> ";
  int prompt_width = utf8::displayWidth(prompt);
  plain(strong);
  plane_->setBold(true);
  plane_->putstr(rows_ - 1, 1, prompt);
  plane_->setBold(false);
  plain(fg);
  string shown_query = utf8::truncateToWidth(query_, inner - prompt_width - 1);
  int query_width = utf8::displayWidth(shown_query);
  plane_->putstr(rows_ - 1, 1 + prompt_width, shown_query);
  set(cursor_fg, cursor_bg);
  plane_->putstr(rows_ - 1, 1 + prompt_width + query_width, " ");
  int hint_width = utf8::displayWidth(kHint);
  if (1 + prompt_width + query_width + 1 + hint_width + 2 <= width_) {
    plain(quiet);
    plane_->putstr(rows_ - 1, width_ - 1 - hint_width, kHint);
  }

  x_ = 0;
  y_ = screen_rows - rows_;
  plane_->move(y_, x_);
  plane_->moveToTop();
}

CommandPalette::Result CommandPalette::offerInput(const Input & input) {
  if (!open_) return Result::CANCELLED;
  auto id = input.getId();

  if (id == Key::Button1) {
    // Resolved on release, like every other click in the app.
    if (input.getKind() != Input::Kind::RELEASE) return Result::NONE;
    int row = input.getY() - y_, col = input.getX() - x_;
    int count = static_cast<int>(shown_.size());
    int index = list_.firstVisible(visibleRows()) + row;
    if (row >= 0 && row < visibleRows() && col >= 0 && col < width_ && index < count) {
      list_.select(index);
      return Result::CHOSEN;
    }
    bool inside_box = input.getY() >= y_ && input.getY() < y_ + rows_ && col >= 0 && col < width_;
    return inside_box ? Result::NONE : Result::CANCELLED;
  }
  // Releases of keys do nothing; everything below acts on the press.
  if (input.getKind() == Input::Kind::RELEASE) return Result::NONE;

  bool ctrl = input.hasCtrl();
  if (id == Key::Enter) return chosen() ? Result::CHOSEN : Result::NONE;
  if (id == Key::Escape || (ctrl && id == 'g')) return Result::CANCELLED;

  int page = max(1, visibleRows() - 1);
  if (id == Key::Up || (ctrl && id == 'p'))
    list_.move(-1);
  else if (id == Key::Down || (ctrl && id == 'n'))
    list_.move(1);
  else if (id == Key::PageUp)
    list_.move(-page);
  else if (id == Key::PageDown)
    list_.move(page);
  else if (id == Key::Button4)
    list_.scroll(-1, visibleRows());
  else if (id == Key::Button5)
    list_.scroll(1, visibleRows());
  else if (id == Key::Tab)
    complete();
  else if (id == Key::Backspace || (ctrl && id == 'h')) {
    // Whole UTF-8 characters off the end.
    while (!query_.empty() && (static_cast<unsigned char>(query_.back()) & 0xC0) == 0x80) query_.pop_back();
    if (!query_.empty()) query_.pop_back();
    refilter();
  } else if (ctrl && id == 'u') {
    query_.clear();
    refilter();
  } else if (!ctrl && !input.hasAlt() && !input.hasMeta() && input.getTrueCaseId() >= 32 && input.getTrueCaseId() < Key::First) {
    query_ += utf8::encodeCodepoint(static_cast<uint32_t>(input.getTrueCaseId()));
    refilter();
  }
  return Result::NONE;
}

}  // namespace cellui
