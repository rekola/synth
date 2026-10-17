#include <cellui/BufferPlane.h>

#include <cellui/Utf8.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>

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
  : UIPlane(std::move(app_context), styles), local_(target.rows(), target.cols()), target_(&target) {
  setDim(std::pair(target.rows(), target.cols()));
  setPosition(std::pair(0, 0));
}

BufferPlane::BufferPlane(BufferPlane & parent)
  : UIPlane(parent.getAppContext(), parent.getStyles()),
    local_(parent.getDim().first, parent.getDim().second), parent_(&parent) {
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
  int extra = overscan_ ? 1 : 0;
  CellBuffer resized(rows + extra, cols + extra);
  for (int r = 0; r < std::min(rows + extra, local_.rows()); r++) {
    for (int c = 0; c < std::min(cols + extra, local_.cols()); c++) resized.put(r, c, local_.at(r, c));
  }
  local_ = std::move(resized);
  UIPlane::resize(rows, cols);
}

void BufferPlane::setScrollOffset(double rows, double cols) {
  if (!root().smooth_scroll_) rows = cols = 0;
  scroll_rows_ = std::clamp(rows, 0.0, 0.999);
  scroll_cols_ = std::clamp(cols, 0.0, 0.999);
  if ((scroll_rows_ > 0 || scroll_cols_ > 0) && !overscan_) {
    overscan_ = true;
    resize(getDim().first, getDim().second);
  }
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
  top.layers_.clear();
  top.elevations_.clear();
  // Lower planes first, whatever their place in the tree; planes of one
  // height in tree order (a parent before its children).
  std::vector<Paint> paints;
  top.collect(paints, 0, 0, 0, 0, top.target_->rows(), top.target_->cols(), 0.0f);
  std::stable_sort(paints.begin(), paints.end(), [](const Paint & a, const Paint & b) { return a.elevation < b.elevation; });
  for (const Paint & p : paints) {
    p.plane->compose(*top.target_, p.row, p.col, p.clip_row, p.clip_col, p.clip_rows, p.clip_cols, p.elevation);
    if (p.elevation <= 0.0f) continue;
    auto [rows, cols] = p.plane->getDim();
    int r0 = std::max(p.clip_row, p.row), r1 = std::min(p.clip_row + p.clip_rows, p.row + rows);
    int c0 = std::max(p.clip_col, p.col), c1 = std::min(p.clip_col + p.clip_cols, p.col + cols);
    if (r0 < r1 && c0 < c1) top.elevations_.push_back(ElevatedArea{ r0, c0, r1 - r0, c1 - c0, p.elevation, p.plane->name_, top.layers_.size() });
  }
  top.collectLifts();
}

// The lifted cells still showing, as rectangles: a run of one height in a row,
// with the same run right below it while that one joins it.
void BufferPlane::collectLifts() {
  const CellBuffer & out = *target_;
  std::vector<ElevatedArea> open;
  for (int r = 0; r < out.rows(); r++) {
    std::vector<ElevatedArea> next;
    for (int c = 0; c < out.cols();) {
      const CellBuffer::Cell & first = out.at(r, c);
      int end = c + 1;
      while (end < out.cols() && out.at(r, end).lift == first.lift && out.at(r, end).lift_joined == first.lift_joined) end++;
      if (first.lift > 0.0f) {
        auto above = std::find_if(open.begin(), open.end(), [&](const ElevatedArea & a) {
          return first.lift_joined && a.col == c && a.cols == end - c && a.elevation == first.lift;
        });
        if (above != open.end()) {
          above->rows++;
          next.push_back(*above);
          open.erase(above);
        } else {
          next.push_back(ElevatedArea{ r, c, 1, end - c, first.lift, "", layers_.size() });
        }
      }
      c = end;
    }
    elevations_.insert(elevations_.end(), open.begin(), open.end());
    open = std::move(next);
  }
  elevations_.insert(elevations_.end(), open.begin(), open.end());
  std::stable_sort(elevations_.begin(), elevations_.end(), [](const ElevatedArea & a, const ElevatedArea & b) { return a.elevation < b.elevation; });
}

void BufferPlane::collect(std::vector<Paint> & paints, int row, int col, int clip_row, int clip_col, int clip_rows, int clip_cols,
                          float base) const {
  if (!visible_) return;
  float height = base + elevation_;
  paints.push_back(Paint{ this, row, col, clip_row, clip_col, clip_rows, clip_cols, height });
  // Children are clipped to the plane's own rectangle.
  int plane_rows = std::min(local_.rows(), getDim().first), plane_cols = std::min(local_.cols(), getDim().second);
  int row_lo = std::max(clip_row, row), row_hi = std::min(clip_row + clip_rows, row + plane_rows);
  int col_lo = std::max(clip_col, col), col_hi = std::min(clip_col + clip_cols, col + plane_cols);
  if (row_lo >= row_hi || col_lo >= col_hi) return;
  for (const BufferPlane * child : children_) {
    child->collect(paints, row + child->getPosition().first, col + child->getPosition().second, row_lo, col_lo, row_hi - row_lo,
                   col_hi - col_lo, height);
  }
}

void BufferPlane::compose(CellBuffer & out, int row, int col, int clip_row, int clip_col, int clip_rows, int clip_cols, float height) const {
  if (!visible_) return;
  const BufferPlane & top = root();
  // The plane's own rectangle, inside the clip.
  int plane_rows = std::min(local_.rows(), getDim().first), plane_cols = std::min(local_.cols(), getDim().second);
  int row_lo = std::max(clip_row, row), row_hi = std::min(clip_row + clip_rows, row + plane_rows);
  int col_lo = std::max(clip_col, col), col_hi = std::min(clip_col + clip_cols, col + plane_cols);
  // A plane shifted by a fraction of a cell is the display's to draw: what is
  // composed below it is set aside, with the plane's cells, as a layer.
  if ((scroll_rows_ > 0 || scroll_cols_ > 0 || band_) && row_lo < row_hi && col_lo < col_hi && row_lo == row && col_lo == col && row_hi == row + plane_rows &&
      col_hi == col + plane_cols) {
    ScrollContent content;
    content.cells = local_;
    if (base_bg_) {
      for (int r = 0; r < content.cells.rows(); r++) {
        for (int c = 0; c < content.cells.cols(); c++) {
          if (!CellBuffer::isBlank(content.cells.at(r, c))) continue;
          CellBuffer::Cell filled;
          filled.bg = base_bg_;
          content.cells.put(r, c, filled);
        }
      }
    }
    content.row = row;
    content.col = col;
    content.rows = plane_rows;
    content.cols = plane_cols;
    content.shift_rows = scroll_rows_;
    content.shift_cols = scroll_cols_;
    content.band = band_;
    top.layers_.push_back(BackdropLayer{ out, Backdrop{}, row, col, plane_rows, plane_cols, std::move(content) });
    out.clear(0, 0, out.rows(), out.cols());
    return;
  }
  // A backdrop is the display's to apply: what is composed below it is set
  // aside as a layer, over the rectangle its cells span, and the cells above
  // it start blank.
  {
    int r_min = row_hi, r_max = row_lo, c_min = col_hi, c_max = col_lo;
    const Backdrop * found = nullptr;
    for (int r = row_lo; r < row_hi; r++) {
      for (int c = col_lo; c < col_hi; c++) {
        const Backdrop & b = local_.at(r - row, c - col).backdrop;
        if (!b.active()) continue;
        if (!found) found = &b;
        r_min = std::min(r_min, r), r_max = std::max(r_max, r + 1);
        c_min = std::min(c_min, c), c_max = std::max(c_max, c + 1);
      }
    }
    if (found) {
      top.layers_.push_back(BackdropLayer{ out, *found, r_min, c_min, r_max - r_min, c_max - c_min, std::nullopt });
      out.clear(0, 0, out.rows(), out.cols());
    }
  }
  for (int r = row_lo; r < row_hi; r++) {
    for (int c = col_lo; c < col_hi; c++) {
      const CellBuffer::Cell * shown = &local_.at(r - row, c - col);
      CellBuffer::Cell without_backdrop;
      bool backdrop_cell = shown->backdrop.active();
      if (backdrop_cell) {
        without_backdrop = *shown;
        without_backdrop.backdrop = {};
        shown = &without_backdrop;
      }
      const CellBuffer::Cell & cell = *shown;
      if (CellBuffer::isBlank(cell)) {
        // A backdrop's own cells show nothing here: the display draws it.
        if (backdrop_cell) continue;
        if (base_bg_) {
          CellBuffer::Cell below = out.at(r, c);
          below.bg = base_bg_;
          below.lift = 0.0f;
          out.put(r, c, below);
        }
        continue;
      }
      // A double-width character is drawn whole or not at all.
      if (cell.continuation && c - 1 < col_lo) continue;
      if (c + 1 < col + local_.cols() && local_.at(r - row, c + 1 - col).continuation && c + 1 >= col_hi) continue;
      CellBuffer::Cell below = out.at(r, c);
      CellBuffer::Cell put = cell;
      if (cell.bg_blend) {
        put.bg = average(cell.bg.value_or(top.default_bg_), below.bg.value_or(top.default_bg_));
        put.bg_blend = false;
      }
      if (cell.fg_contrast) {
        put.fg = contrasting(cell.fg.value_or(top.default_fg_), put.bg.value_or(top.default_bg_));
        put.fg_contrast = false;
      }
      put.lift = cell.lift > 0.0f ? height + cell.lift : 0.0f;
      out.put(r, c, put);
    }
  }
  // An image shows its pixels in place of the cells it covers: they are
  // marked for the backend, which draws the image wherever they stay marked.
  int cell_h = top.cell_px_h_, cell_w = top.cell_px_w_;
  for (const PlacedImage & placed : images_) {
    if (cell_h <= 0 || cell_w <= 0 || row_lo >= row_hi || col_lo >= col_hi) break;
    int top_row = row + placed.row, left_col = col + placed.col;
    int r_hi = std::min(row_hi, top_row + placed.image->coveredRows(cell_h));
    int c_hi = std::min(col_hi, left_col + placed.image->coveredCols(cell_w));
    int r_lo = std::max(row_lo, top_row), c_lo = std::max(col_lo, left_col);
    if (placed.clip_top != INT_MIN) r_lo = std::max(r_lo, row + placed.clip_top);
    if (placed.clip_bottom != INT_MAX) r_hi = std::min(r_hi, row + placed.clip_bottom);
    if (r_lo >= r_hi || c_lo >= c_hi) continue;
    ImagePlacement placement;
    placement.image = placed.image;
    placement.key = placed.key;
    placement.row = top_row;
    placement.col = left_col;
    placement.index = static_cast<int>(top.placements_.size());
    top.placements_.push_back(placement);
    CellBuffer::Cell marked;
    marked.image = placement.index + 1;
    for (int r = r_lo; r < r_hi; r++) {
      for (int c = c_lo; c < c_hi; c++) {
        // A picture on a lifted cell stands with it.
        marked.lift = out.at(r, c).lift;
        marked.lift_joined = out.at(r, c).lift_joined;
        out.put(r, c, marked);
      }
    }
  }
}

bool BufferPlane::pixelSize(int & height, int & width) const {
  const BufferPlane & top = root();
  if (top.cell_px_h_ <= 0 || top.cell_px_w_ <= 0) return false;
  height = getDim().first * top.cell_px_h_;
  width = getDim().second * top.cell_px_w_;
  return height > 0 && width > 0;
}

bool BufferPlane::cellPixelSize(int & height, int & width) const {
  const BufferPlane & top = root();
  if (top.cell_px_h_ <= 0 || top.cell_px_w_ <= 0) return false;
  height = top.cell_px_h_;
  width = top.cell_px_w_;
  return true;
}

void BufferPlane::drawImage(const std::vector<uint32_t> & pixels, int width, int height, uint64_t id) {
  // A whole-plane image replaces any others.
  images_.erase(std::remove_if(images_.begin(), images_.end(), [](const PlacedImage & p) { return p.row != 0 || p.col != 0; }), images_.end());
  drawImageAt(0, 0, pixels, width, height, id);
}

void BufferPlane::drawImageAt(int row, int col, const std::vector<uint32_t> & pixels, int width, int height, uint64_t id, int clip_top,
                              int clip_bottom) {
  if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * static_cast<size_t>(height)) return;
  size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
  bool explicit_id = id != 0;
  uint64_t identity = id;
  if (!explicit_id) {
    // FNV-1a over the size and the pixels.
    identity = 14695981039346656037ull;
    auto mix = [&](uint64_t v) { identity = (identity ^ v) * 1099511628211ull; };
    mix(static_cast<uint64_t>(width));
    mix(static_cast<uint64_t>(height));
    for (size_t i = 0; i < count; i++) mix(pixels[i]);
  }
  auto equal = [&](const PlacedImage & placed) {
    return placed.image->width == width && placed.image->height == height &&
           std::equal(placed.image->pixels.begin(), placed.image->pixels.end(), pixels.begin());
  };
  auto fresh = [&]() {
    auto image = std::make_shared<Image>();
    image->width = width;
    image->height = height;
    image->pixels.assign(pixels.begin(), pixels.begin() + static_cast<long>(count));
    image->version = nextImageVersion();
    return image;
  };
  // The same picture again at the same place keeps its version and key, so
  // it is not sent again; other pixels there are the slot's image changed.
  for (PlacedImage & placed : images_) {
    if (placed.row != row || placed.col != col) continue;
    placed.clip_top = clip_top;
    placed.clip_bottom = clip_bottom;
    placed.identity = identity;
    placed.explicit_id = explicit_id;
    if (!equal(placed)) placed.image = fresh();
    return;
  }
  // A picture the plane showed before its last erase() is the same image,
  // wherever it is drawn now.
  for (auto it = erased_images_.begin(); it != erased_images_.end(); ++it) {
    if (it->identity != identity || it->explicit_id != explicit_id) continue;
    PlacedImage placed = *it;
    erased_images_.erase(it);
    placed.row = row;
    placed.col = col;
    placed.clip_top = clip_top;
    placed.clip_bottom = clip_bottom;
    if (!equal(placed)) placed.image = fresh();
    images_.push_back(std::move(placed));
    return;
  }
  images_.push_back(PlacedImage{row, col, fresh(), nextKey(), identity, explicit_id, clip_top, clip_bottom});
}

std::pair<int, int> BufferPlane::screenPosition() const {
  auto [y, x] = getPosition();
  if (!parent_) return {y, x};
  auto [py, px] = parent_->screenPosition();
  return {py + y, px + x};
}

PlaneInfo BufferPlane::describe() const {
  PlaneInfo info;
  info.name = name_;
  std::tie(info.y, info.x) = getPosition();
  std::tie(info.rows, info.cols) = getDim();
  info.visible = visible_;
  info.elevation = elevation_;
  info.reader = editor_ != nullptr;
  info.picker = picker_ != nullptr;
  for (const BufferPlane * child : children_) info.children.push_back(child->describe());
  return info;
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
    indicator_->setElevation(getStyles().theme.elevation.overlay);
    indicator_->resize(1, kIndicatorCols);
    indicator_->style_.fg = Color(0xff, 0xff, 0xff);
  }
  indicator_->move(0, reader_x_ + x);
  indicator_->erase();
  indicator_->putstr(0, 0, s);
  indicator_->moveToTop();
}

// The picker: an opaque panel with no border, a row of padding, the items
// (each option right-aligned to the longest, then its description), a row of
// padding. The list wraps around at both ends.

void BufferPlane::showPicker(int y, int x, int rows, int cols, int item_count) {
  if (picker_) return;
  picker_ = std::make_unique<Picker>();
  picker_->plane = makeChild();
  picker_->plane->setElevation(getStyles().theme.elevation.overlay);
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
  int rows = std::min(std::max(shown, 1) + 2, getDim().first - pk.y);
  shown = std::max(0, std::min(shown, rows - 2));
  p.resize(std::max(rows, 1), body_cols);
  p.erase();

  CellBuffer::Style base;
  base.fg = theme.picker_fg_color;
  base.bg = theme.picker_bg_color;
  CellBuffer::Style option;
  option.fg = theme.highlight_bg_color;
  option.bg = theme.highlight_fg_color;
  CellBuffer::Style option_selected;
  option_selected.fg = theme.highlight_fg_color;
  option_selected.bg = theme.highlight_bg_color;
  CellBuffer::Style desc = base;
  desc.fg = theme.picker_bg_color;

  for (int r = 0; r < rows; r++) p.local_.write(r, 0, std::string(static_cast<size_t>(body_cols), ' '), base);
  if (count == 0 || shown == 0) return;
  // Keep the selection inside the window, which wraps around.
  int offset = ((pk.selected - pk.first) % count + count) % count;
  if (offset >= shown) pk.first = ((pk.selected - shown + 1) % count + count) % count;
  for (int i = 0; i < shown; i++) {
    int item = (pk.first + i) % count;
    bool selected = item == pk.selected;
    const std::string & text = pk.options[static_cast<size_t>(item)];
    int col = 2 + longest_option - utf8::displayWidth(text);
    p.local_.write(1 + i, col, text, selected ? option_selected : option);
    CellBuffer::Style d = desc;
    if (selected) std::swap(d.fg, d.bg);
    p.local_.write(1 + i, 2 + longest_option, " " + pk.descriptions[static_cast<size_t>(item)], d);
  }
  if (shown < count) {
    CellBuffer::Style arrow = desc;
    arrow.fg = theme.highlight_bg_color;
    p.local_.write(0, 2 + longest_option, "↑", arrow);
    p.local_.write(rows - 1, 2 + longest_option, "↓", arrow);
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
  int shown = std::max(1, pk.plane->getDim().first - 2);
  int id = input.getId();
  bool release = input.getKind() == Input::Kind::RELEASE;
  if (id == Key::Button1) {
    if (!release) return false;
    auto [py, px] = pk.plane->screenPosition();
    int row = input.getY() - py - 1, col = input.getX() - px;
    if (row < 0 || row >= shown || col < 0 || col >= pk.plane->getDim().second) return false;
    pk.selected = (pk.first + row) % count;
    drawPicker();
    return true;
  }
  if (release) return false;
  if (id == Key::Up || id == Key::Button4) move(-1);
  else if (id == Key::Down || id == Key::Button5) move(1);
  else if (id == Key::PageUp) move(-shown);
  else if (id == Key::PageDown) move(shown);
  else return false;
  return true;
}

}  // namespace cellui
