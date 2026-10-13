#include <cellui/BufferPlane.h>

#include <cellui/Utf8.h>

#include <algorithm>

namespace cellui {

namespace {

Color average(const Color & a, const Color & b) {
  return Color((a.getRed() + b.getRed()) / 2, (a.getGreen() + b.getGreen()) / 2, (a.getBlue() + b.getBlue()) / 2);
}

// Three parts white or black (whichever stands out against `bg`) to one
// part `fg`.
Color contrasting(const Color & fg, const Color & bg) {
  int c = bg.getRed() + bg.getGreen() + bg.getBlue() < 320 ? 255 : 0;
  return Color((3 * c + fg.getRed()) / 4, (3 * c + fg.getGreen()) / 4, (3 * c + fg.getBlue()) / 4);
}

// Wide enough for the longest status the reader's indicator shows.
constexpr int kIndicatorCols = 20;

uint64_t nextKey() {
  static uint64_t key = 0;
  return ++key;
}

uint64_t nextImageVersion() {
  static uint64_t version = 0;
  return ++version;
}

}  // namespace

struct BufferPlane::Picker {
  std::unique_ptr<BufferPlane> plane;
  std::vector<std::string> options, descriptions;
  int selected = 0;
  int first = 0;        // the first item shown
  int max_display = 0;  // 0: as many as fit
  int y = 0, x = 0;

  int count() const { return static_cast<int>(options.size()); }
};

BufferPlane::BufferPlane(CellBuffer & target, const StyleProvider & styles, std::shared_ptr<void> app_context)
  : UIPlane(std::move(app_context), styles), local_(target.rows(), target.cols()), target_(&target), key_(nextKey()) {
  setDim(std::pair(target.rows(), target.cols()));
  setPosition(std::pair(0, 0));
}

BufferPlane::BufferPlane(BufferPlane & parent)
  : UIPlane(parent.getAppContext(), parent.getStyles()),
    local_(parent.getDim().first, parent.getDim().second), parent_(&parent), key_(nextKey()) {
  setDim(parent.getDim());
  setPosition(std::pair(0, 0));
  parent.children_.push_back(this);
}

BufferPlane::~BufferPlane() {
  if (editor_ && root().active_reader_ == this) root().active_reader_ = nullptr;
  for (BufferPlane * child : children_) child->parent_ = nullptr;
  if (parent_) parent_->children_.erase(std::remove(parent_->children_.begin(), parent_->children_.end(), this), parent_->children_.end());
}

void BufferPlane::resize(int rows, int cols) {
  rows = std::max(0, rows);
  cols = std::max(0, cols);
  CellBuffer resized(rows, cols);
  for (int r = 0; r < std::min(rows, local_.rows()); r++) {
    for (int c = 0; c < std::min(cols, local_.cols()); c++) resized.put(r, c, local_.at(r, c));
  }
  local_ = std::move(resized);
  UIPlane::resize(rows, cols);
}

void BufferPlane::moveToTop() {
  if (!parent_) return;
  auto & siblings = parent_->children_;
  siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
  siblings.push_back(this);
}

void BufferPlane::putstr(int y, int x, const std::string & s) {
  local_.write(y, x, s, style_);
}

std::unique_ptr<UIPlane> BufferPlane::createChild() {
  BufferPlane & parent = root().flat_ ? root() : *this;
  auto child = std::unique_ptr<BufferPlane>(new BufferPlane(parent));
  if (root().themed_bases_) child->base_bg_ = getStyles().plane_base_bg_color;
  return child;
}

void BufferPlane::drawBorder() {
  auto [rows, cols] = getDim();
  if (rows < 2 || cols < 2) return;
  std::string horizontal;
  for (int c = 0; c < cols - 2; c++) horizontal += "─";
  putstr(0, 0, "╭" + horizontal + "╮");
  putstr(rows - 1, 0, "╰" + horizontal + "╯");
  for (int r = 1; r < rows - 1; r++) {
    putstr(r, 0, "│");
    putstr(r, cols - 1, "│");
  }
}

void BufferPlane::refresh() {
  BufferPlane & top = root();
  if (!top.target_) return;
  top.target_->clear(0, 0, top.target_->rows(), top.target_->cols());
  top.placements_.clear();
  top.compose(*top.target_, 0, 0, 0, 0, top.target_->rows(), top.target_->cols());
}

void BufferPlane::compose(CellBuffer & out, int row, int col, int clip_row, int clip_col, int clip_rows, int clip_cols) const {
  if (!visible_) return;
  const BufferPlane & top = root();
  // The plane's own rectangle, inside the clip.
  int row_lo = std::max(clip_row, row), row_hi = std::min(clip_row + clip_rows, row + local_.rows());
  int col_lo = std::max(clip_col, col), col_hi = std::min(clip_col + clip_cols, col + local_.cols());
  if (image_ && row_lo < row_hi && col_lo < col_hi) {
    // An image plane shows its pixels, not its text: its cells are marked
    // for the backend, which draws the image wherever they stay marked.
    ImagePlacement placement;
    placement.image = image_;
    placement.key = key_;
    placement.row = row;
    placement.col = col;
    placement.index = static_cast<int>(top.placements_.size());
    top.placements_.push_back(placement);
    CellBuffer::Cell marked;
    marked.image = placement.index + 1;
    for (int r = row_lo; r < row_hi; r++) {
      for (int c = col_lo; c < col_hi; c++) out.put(r, c, marked);
    }
  }
  for (int r = row_lo; r < row_hi && !image_; r++) {
    for (int c = col_lo; c < col_hi; c++) {
      const CellBuffer::Cell & cell = local_.at(r - row, c - col);
      if (CellBuffer::isBlank(cell)) {
        if (base_bg_) {
          CellBuffer::Cell below = out.at(r, c);
          below.bg = base_bg_;
          out.put(r, c, below);
        }
        continue;
      }
      // A double-width character is drawn whole or not at all.
      if (cell.continuation && c - 1 < col_lo) continue;
      if (c + 1 < col + local_.cols() && local_.at(r - row, c + 1 - col).continuation && c + 1 >= col_hi) continue;
      CellBuffer::Cell put = cell;
      if (cell.bg_blend) {
        const CellBuffer::Cell & below = out.at(r, c);
        put.bg = average(cell.bg.value_or(top.default_bg_), below.bg.value_or(top.default_bg_));
        put.bg_blend = false;
      }
      if (cell.fg_contrast) {
        put.fg = contrasting(cell.fg.value_or(top.default_fg_), put.bg.value_or(top.default_bg_));
        put.fg_contrast = false;
      }
      out.put(r, c, put);
    }
  }
  if (row_lo >= row_hi || col_lo >= col_hi) return;
  for (const BufferPlane * child : children_) {
    child->compose(out, row + child->getPosition().first, col + child->getPosition().second,
                   row_lo, col_lo, row_hi - row_lo, col_hi - col_lo);
  }
}

bool BufferPlane::pixelSize(int & height, int & width) const {
  const BufferPlane & top = root();
  if (top.cell_px_h_ <= 0 || top.cell_px_w_ <= 0) return false;
  height = getDim().first * top.cell_px_h_;
  width = getDim().second * top.cell_px_w_;
  return height > 0 && width > 0;
}

void BufferPlane::drawImage(const std::vector<uint32_t> & pixels, int width, int height) {
  if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * static_cast<size_t>(height)) return;
  auto image = std::make_shared<Image>();
  image->width = width;
  image->height = height;
  image->pixels.assign(pixels.begin(), pixels.begin() + static_cast<long>(width) * height);
  image->version = nextImageVersion();
  image_ = std::move(image);
}

std::pair<int, int> BufferPlane::screenPosition() const {
  auto [y, x] = getPosition();
  if (!parent_) return {y, x};
  auto [py, px] = parent_->screenPosition();
  return {py + y, px + x};
}

bool BufferPlane::shown() const {
  return visible_ && (!parent_ || parent_->shown());
}

std::optional<std::pair<int, int>> BufferPlane::cursorPosition() const {
  const BufferPlane * reader = root().active_reader_;
  if (!reader || !reader->reader_plane_ || !reader->reader_plane_->shown()) return std::nullopt;
  auto [y, x] = reader->reader_plane_->screenPosition();
  return std::pair(y, x + reader->reader_cursor_);
}

// The reader.

void BufferPlane::showReader(const std::string & prompt, int y, int x, int rows, int cols, const std::string & initial_text,
                             std::optional<Color> text_color) {
  if (editor_) return;
  // Whatever was on the row (a longer earlier message) goes, from the
  // field's column on.
  int erase_x = x == -1 ? 0 : x;
  local_.clear(y, erase_x, 1, std::max(getDim().second - erase_x, 1));
  if (!prompt.empty()) putstr(y, 0, prompt);
  reader_x_ = x == -1 ? utf8::displayWidth(prompt) : x;
  auto [plane_rows, plane_cols] = getDim();
  int reader_rows = rows == -1 ? plane_rows : rows;
  int reader_cols = cols == -1 ? std::max(1, plane_cols - reader_x_) : std::max(1, cols);
  reader_color_ = text_color.value_or(getStyles().reader_text_color);
  reader_plane_ = makeChild();
  reader_plane_->move(y, reader_x_);
  reader_plane_->resize(std::max(1, reader_rows), reader_cols);
  editor_ = std::make_unique<LineEditor>();
  editor_->setText(initial_text);
  root().active_reader_ = this;
  drawReader();
}

void BufferPlane::drawReader() {
  if (!editor_ || !reader_plane_) return;
  BufferPlane & p = *reader_plane_;
  p.erase();
  p.style_ = CellBuffer::Style{};
  p.style_.fg = reader_color_;
  LineEditor::View v = editor_->view(p.getDim().second);
  p.putstr(0, 0, v.text);
  reader_cursor_ = v.cursor_column;
}

std::string BufferPlane::closeReader() {
  std::string text = editor_ ? editor_->text() : "";
  if (root().active_reader_ == this) root().active_reader_ = nullptr;
  editor_.reset();
  reader_plane_.reset();
  indicator_.reset();
  return text;
}

void BufferPlane::setReaderContents(const std::string & text) {
  if (!editor_) return;
  editor_->setText(text);
  drawReader();
}

void BufferPlane::insertReaderText(const std::string & text) {
  if (!editor_) return;
  editor_->insert(text);
  drawReader();
}

bool BufferPlane::insertIntoReader(const std::string & text) {
  BufferPlane * reader = root().active_reader_;
  if (!reader) return false;
  std::string line = text;
  std::replace(line.begin(), line.end(), '\n', ' ');
  reader->insertReaderText(line);
  return true;
}

void BufferPlane::showReaderIndicator(int x, const std::string & s) {
  if (!editor_) return;
  if (!indicator_) {
    indicator_ = makeChild();
    indicator_->resize(1, kIndicatorCols);
    indicator_->style_.fg = Color(0xff, 0xff, 0xff);
  }
  indicator_->move(0, reader_x_ + x);
  indicator_->erase();
  indicator_->putstr(0, 0, s);
  indicator_->moveToTop();
}

// The picker: a rounded box, a blank row, the items (each option right-
// aligned to the longest, then its description), a blank row. The list
// wraps around at both ends.

void BufferPlane::showPicker(int y, int x, int rows, int cols, int item_count) {
  if (picker_) return;
  picker_ = std::make_unique<Picker>();
  picker_->plane = makeChild();
  picker_->y = y;
  picker_->x = x;
  picker_->plane->move(y, x);
  picker_->plane->resize(std::max(1, rows), std::max(1, cols));
  picker_->max_display = std::max(0, item_count);
  picker_->plane->moveToTop();
  drawPicker();
}

void BufferPlane::addItem(const std::string & id, const std::string & description) {
  if (!picker_) return;
  picker_->options.push_back(id);
  picker_->descriptions.push_back(description);
  drawPicker();
}

void BufferPlane::selectPickerItem(int index) {
  if (!picker_ || index < 0 || index >= picker_->count()) return;
  picker_->selected = index;
  drawPicker();
}

void BufferPlane::closePicker() { picker_.reset(); }

std::string BufferPlane::getPickerSelection() const {
  if (!picker_ || picker_->options.empty()) return "";
  return picker_->options[static_cast<size_t>(picker_->selected)];
}

void BufferPlane::drawPicker() {
  Picker & pk = *picker_;
  BufferPlane & p = *pk.plane;
  const StyleProvider & theme = getStyles();
  int longest_option = 0, longest_desc = 0;
  for (size_t i = 0; i < pk.options.size(); i++) {
    longest_option = std::max(longest_option, utf8::displayWidth(pk.options[i]));
    longest_desc = std::max(longest_desc, utf8::displayWidth(pk.descriptions[i]));
  }
  int count = pk.count();
  int shown = pk.max_display > 0 ? std::min(pk.max_display, count) : count;
  int body_cols = longest_option + longest_desc + 5;
  int rows = std::min(std::max(shown, 1) + 4, getDim().first - pk.y);
  shown = std::max(0, std::min(shown, rows - 4));
  p.resize(std::max(rows, 1), body_cols);
  p.erase();

  CellBuffer::Style base;
  base.fg = theme.picker_fg_color;
  base.bg = theme.picker_bg_color;
  base.bg_blend = true;
  CellBuffer::Style box = base;
  box.fg = theme.picker_border_color;
  CellBuffer::Style option;
  option.fg = theme.highlight_bg_color;
  option.bg = theme.highlight_fg_color;
  CellBuffer::Style option_selected;
  option_selected.fg = theme.highlight_fg_color;
  option_selected.bg = theme.highlight_bg_color;
  CellBuffer::Style desc = base;
  desc.fg = theme.picker_bg_color;

  for (int r = 0; r < rows; r++) p.local_.write(r, 0, std::string(static_cast<size_t>(body_cols), ' '), base);
  std::string horizontal;
  for (int c = 0; c < body_cols - 2; c++) horizontal += "─";
  p.local_.write(0, 0, "╭" + horizontal + "╮", box);
  p.local_.write(rows - 1, 0, "╰" + horizontal + "╯", box);
  for (int r = 1; r < rows - 1; r++) {
    p.local_.write(r, 0, "│", box);
    p.local_.write(r, body_cols - 1, "│", box);
  }
  if (count == 0 || shown == 0) return;
  // Keep the selection inside the window, which wraps around.
  int offset = ((pk.selected - pk.first) % count + count) % count;
  if (offset >= shown) pk.first = ((pk.selected - shown + 1) % count + count) % count;
  for (int i = 0; i < shown; i++) {
    int item = (pk.first + i) % count;
    bool selected = item == pk.selected;
    const std::string & text = pk.options[static_cast<size_t>(item)];
    int col = 2 + longest_option - utf8::displayWidth(text);
    p.local_.write(2 + i, col, text, selected ? option_selected : option);
    CellBuffer::Style d = desc;
    if (selected) std::swap(d.fg, d.bg);
    p.local_.write(2 + i, 2 + longest_option, " " + pk.descriptions[static_cast<size_t>(item)], d);
  }
  if (shown < count) {
    CellBuffer::Style arrow = desc;
    arrow.fg = theme.highlight_bg_color;
    p.local_.write(1, 2 + longest_option, "↑", arrow);
    p.local_.write(rows - 2, 2 + longest_option, "↓", arrow);
  }
}

bool BufferPlane::offerInput(const Input & input) {
  if (editor_) {
    editor_->offerInput(input);
    drawReader();
    return true;
  }
  if (!picker_) return false;
  Picker & pk = *picker_;
  int count = pk.count();
  if (count == 0) return false;
  auto move = [&](int delta) {
    pk.selected = ((pk.selected + delta) % count + count) % count;
    drawPicker();
  };
  int shown = std::max(1, pk.plane->getDim().first - 4);
  int id = input.getId();
  bool release = input.getKind() == Input::Kind::RELEASE;
  if (id == NCKEY_BUTTON1) {
    if (!release) return false;
    auto [py, px] = pk.plane->screenPosition();
    int row = input.getY() - py - 2, col = input.getX() - px;
    if (row < 0 || row >= shown || col < 0 || col >= pk.plane->getDim().second) return false;
    pk.selected = (pk.first + row) % count;
    drawPicker();
    return true;
  }
  if (release) return false;
  if (id == NCKEY_UP || id == NCKEY_BUTTON4) move(-1);
  else if (id == NCKEY_DOWN || id == NCKEY_BUTTON5) move(1);
  else if (id == NCKEY_PGUP) move(-shown);
  else if (id == NCKEY_PGDOWN) move(shown);
  else return false;
  return true;
}

}  // namespace cellui
