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

uint32_t firstCodepoint(const std::string & s) {
  if (s.empty()) return 0;
  auto b = [&](size_t i) { return i < s.size() ? static_cast<uint32_t>(static_cast<unsigned char>(s[i])) : 0u; };
  uint32_t c = b(0);
  if (c < 0x80) return c;
  if ((c >> 5) == 6) return ((c & 0x1F) << 6) | (b(1) & 0x3F);
  if ((c >> 4) == 14) return ((c & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F);
  return ((c & 0x07) << 18) | ((b(1) & 0x3F) << 12) | ((b(2) & 0x3F) << 6) | (b(3) & 0x3F);
}

// Roughly how much of its cell a glyph inks: what a blurred cell's colour
// mixes its foreground in by.
float inkCoverage(const std::string & text) {
  uint32_t cp = firstCodepoint(text);
  if (cp == 0 || cp == ' ') return 0.0f;
  if (cp == 0x2588) return 1.0f;                                                 // full block
  if (cp >= 0x2581 && cp <= 0x2587) return static_cast<float>(cp - 0x2580) / 8;  // lower eighths
  if (cp >= 0x2589 && cp <= 0x258F) return static_cast<float>(0x2590 - cp) / 8;  // left eighths
  if (cp == 0x2580 || cp == 0x2590) return 0.5f;                                 // upper, right half
  if (cp >= 0x2591 && cp <= 0x2593) return static_cast<float>(cp - 0x2590) / 4;  // shades
  if (cp >= 0x2800 && cp <= 0x28FF) {                                            // braille dots
    int dots = 0;
    for (uint32_t bits = cp - 0x2800; bits; bits >>= 1) dots += static_cast<int>(bits & 1);
    return 0.06f * static_cast<float>(dots);
  }
  if (cp >= 0x2500 && cp <= 0x257F) return 0.15f;  // box drawing
  return 0.2f;
}

// One pass of a box blur along rows or columns, the window shrinking at the
// edges.
void blurPass(std::vector<uint32_t> & px, int w, int h, int radius, bool along_rows) {
  int lines = along_rows ? h : w, length = along_rows ? w : h;
  std::vector<std::array<int, 4>> line(static_cast<size_t>(length));
  auto at = [&](int l, int i) -> uint32_t & {
    return px[along_rows ? static_cast<size_t>(l) * static_cast<size_t>(w) + static_cast<size_t>(i)
                         : static_cast<size_t>(i) * static_cast<size_t>(w) + static_cast<size_t>(l)];
  };
  for (int l = 0; l < lines; l++) {
    for (int i = 0; i < length; i++) {
      uint32_t p = at(l, i);
      line[static_cast<size_t>(i)] = {Image::red(p), Image::green(p), Image::blue(p), Image::alpha(p)};
    }
    std::array<int, 4> sum = {0, 0, 0, 0};
    int lo = 0, hi = 0;  // the window [lo, hi)
    for (int i = 0; i < length; i++) {
      for (; hi < std::min(length, i + radius + 1); hi++)
        for (size_t k = 0; k < 4; k++) sum[k] += line[static_cast<size_t>(hi)][k];
      for (; lo < i - radius; lo++)
        for (size_t k = 0; k < 4; k++) sum[k] -= line[static_cast<size_t>(lo)][k];
      int n = hi - lo;
      at(l, i) = Image::pack(static_cast<uint8_t>(sum[0] / n), static_cast<uint8_t>(sum[1] / n), static_cast<uint8_t>(sum[2] / n),
                             static_cast<uint8_t>(sum[3] / n));
    }
  }
}

// Three box passes, close to a Gaussian (two leave a blur's corners
// square).
constexpr int kBlurPasses = 3;

// The box radius, in pixels, of a backdrop's blur for cells `cell_h`
// pixels tall drawn `scale` times smaller: as strong as two passes of half a
// row per step of blur (a box of radius r adds r(r + 1) / 3 to the
// variance).
int boxRadius(int blur, int cell_h, int scale) {
  double two_pass = std::min(blur, Backdrop::kMaxBlur) * cell_h / 2 / std::max(1, scale);
  if (two_pass <= 0) return 0;
  double variance = 2 * two_pass * (two_pass + 1) / 3;
  return std::max(1, static_cast<int>(std::lround((std::sqrt(1 + 4 * variance * 3 / kBlurPasses) - 1) / 2)));
}

// Blurs the pixels over `radius` and dims them toward `tint`.
void filterPixels(Image & image, const Backdrop & backdrop, int radius, const Color & tint) {
  if (radius > 0) {
    for (int pass = 0; pass < kBlurPasses; pass++) {
      blurPass(image.pixels, image.width, image.height, radius, true);
      blurPass(image.pixels, image.width, image.height, radius, false);
    }
  }
  if (backdrop.dim > 0.0f) {
    for (auto & px : image.pixels) {
      Color c = Color(Image::red(px), Image::green(px), Image::blue(px)).blend(backdrop.dim, tint);
      px = Image::pack(static_cast<uint8_t>(c.getRed()), static_cast<uint8_t>(c.getGreen()), static_cast<uint8_t>(c.getBlue()),
                       Image::alpha(px));
    }
  }
}

// What a backdrop tile is drawn from, as one number: the cells, and the
// pictures under them by identity and the part of them each cell shows.
class Fingerprint {
 public:
  void add(uint64_t v) {
    for (int i = 0; i < 8; i++) {
      hash_ ^= (v >> (8 * i)) & 0xFF;
      hash_ *= 0x100000001b3ULL;
    }
  }
  void add(const std::string & s) {
    for (char ch : s) {
      hash_ ^= static_cast<unsigned char>(ch);
      hash_ *= 0x100000001b3ULL;
    }
    add(s.size());
  }
  void add(const std::optional<Color> & c) {
    uint64_t v = 0;
    if (c) v = uint64_t{1} << 24 | static_cast<uint64_t>(c->getRed()) << 16 | static_cast<uint64_t>(c->getGreen()) << 8 | static_cast<uint64_t>(c->getBlue());
    add(v);
  }
  uint64_t value() const { return hash_; }

 private:
  uint64_t hash_ = 0xcbf29ce484222325ULL;
};

// The layers below a plane with backdrop cells, kept as they were before
// the plane drew, and each of their cells as a backdrop shows it.
class BackdropSource {
 public:
  BackdropSource(const CellBuffer & below, const std::vector<ImagePlacement> & images, int cell_h, int cell_w, Color fg, Color bg)
    : below_(below), images_(images), cell_h_(cell_h), cell_w_(cell_w), default_fg_(fg), default_bg_(bg),
      sums_(static_cast<size_t>(below.rows() + 1) * static_cast<size_t>(below.cols() + 1)) {
    // A summed-area table of the cells' colours, so any blur radius is four
    // lookups.
    for (int r = 0; r < below.rows(); r++) {
      for (int c = 0; c < below.cols(); c++) {
        Color tone = toneOf(r, c);
        auto & s = sum(r + 1, c + 1);
        const auto & up = sum(r, c + 1), & left = sum(r + 1, c), & diag = sum(r, c);
        s = {up[0] + left[0] - diag[0] + tone.getRed(), up[1] + left[1] - diag[1] + tone.getGreen(),
             up[2] + left[2] - diag[2] + tone.getBlue()};
      }
    }
  }

  const CellBuffer & below() const { return below_; }

  CellBuffer::Cell seen(int row, int col, const Backdrop & b) const {
    CellBuffer::Cell cell = below_.at(row, col);
    Color fg = cell.fg.value_or(default_fg_), bg = cell.bg.value_or(default_bg_);
    if (b.blur > 0) {
      int radius = std::min(b.blur, Backdrop::kMaxBlur);
      // A glyph's ink goes into the colour; the cell keeps no glyph, so
      // nothing behind the backdrop can be read or selected.
      bg = average(row - radius, col - 2 * radius, row + radius + 1, col + 2 * radius + 1);
      fg = bg;
      if (!cell.image) {
        cell.text = " ";
        cell.continuation = cell.bold = cell.italic = cell.underline = false;
      }
    }
    if (b.dim > 0.0f) {
      Color tint = b.tint.value_or(default_bg_);
      fg = fg.blend(b.dim, tint);
      bg = bg.blend(b.dim, tint);
    }
    cell.fg = fg;
    cell.bg = bg;
    cell.bg_blend = cell.fg_contrast = false;
    cell.backdrop = {};
    return cell;
  }

 private:
  using Sum = std::array<long, 3>;
  Sum & sum(int r, int c) { return sums_[static_cast<size_t>(r) * static_cast<size_t>(below_.cols() + 1) + static_cast<size_t>(c)]; }
  const Sum & sum(int r, int c) const {
    return sums_[static_cast<size_t>(r) * static_cast<size_t>(below_.cols() + 1) + static_cast<size_t>(c)];
  }

  // The cells in rows [r0, r1) and columns [c0, c1), inside the buffer.
  Color average(int r0, int c0, int r1, int c1) const {
    r0 = std::max(r0, 0), c0 = std::max(c0, 0), r1 = std::min(r1, below_.rows()), c1 = std::min(c1, below_.cols());
    long n = static_cast<long>(r1 - r0) * (c1 - c0);
    if (n <= 0) return default_bg_;
    int ch[3];
    for (size_t k = 0; k < 3; k++) ch[k] = static_cast<int>((sum(r1, c1)[k] - sum(r0, c1)[k] - sum(r1, c0)[k] + sum(r0, c0)[k]) / n);
    return Color(ch[0], ch[1], ch[2]);
  }

  // A cell's colour as the eye averages it: its background with its glyph's
  // ink, or an image's pixels over the display's background.
  Color toneOf(int r, int c) const {
    const CellBuffer::Cell & cell = below_.at(r, c);
    Color bg = cell.bg.value_or(default_bg_);
    if (cell.image > 0 && static_cast<size_t>(cell.image) <= images_.size() && cell_h_ > 0 && cell_w_ > 0) {
      const ImagePlacement & p = images_[static_cast<size_t>(cell.image - 1)];
      const Image & img = *p.image;
      int y0, y1, x0, x1;
      img.pixelSpan(r - p.row, 1, cell_h_, false, y0, y1);
      img.pixelSpan(c - p.col, 1, cell_w_, true, x0, x1);
      if (y0 >= y1 || x0 >= x1) return default_bg_;
      long ch[3] = {0, 0, 0};
      for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
          uint32_t px = img.pixels[static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x)];
          int a = Image::alpha(px);
          ch[0] += (Image::red(px) * a + default_bg_.getRed() * (255 - a)) / 255;
          ch[1] += (Image::green(px) * a + default_bg_.getGreen() * (255 - a)) / 255;
          ch[2] += (Image::blue(px) * a + default_bg_.getBlue() * (255 - a)) / 255;
        }
      }
      long n = static_cast<long>(y1 - y0) * (x1 - x0);
      return Color(static_cast<int>(ch[0] / n), static_cast<int>(ch[1] / n), static_cast<int>(ch[2] / n));
    }
    float ink = inkCoverage(cell.text);
    return ink > 0.0f ? bg.blend(ink, cell.fg.value_or(default_fg_)) : bg;
  }

  CellBuffer below_;
  const std::vector<ImagePlacement> & images_;
  int cell_h_, cell_w_;
  Color default_fg_, default_bg_;
  std::vector<Sum> sums_;
};

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
  for (auto & f : top.filtered_images_) f.used = false;
  for (auto & t : top.backdrop_tiles_) t.used = false;
  top.redrawn_tiles_ = 0;
  top.compose(*top.target_, 0, 0, 0, 0, top.target_->rows(), top.target_->cols());
  auto & cache = top.filtered_images_;
  cache.erase(std::remove_if(cache.begin(), cache.end(), [](const FilteredImage & f) { return !f.used; }), cache.end());
  auto & tiles = top.backdrop_tiles_;
  tiles.erase(std::remove_if(tiles.begin(), tiles.end(), [](const BackdropTile & t) { return !t.used; }), tiles.end());
}

int BufferPlane::backdropTile(const CellBuffer & below, int row, int col, int rows, int cols, const Backdrop & backdrop) const {
  int blur = std::min(backdrop.blur, Backdrop::kMaxBlur);
  // Drawn smaller than the display shows it: a blur needs no more detail.
  int ch = cell_px_h_, cw = cell_px_w_;
  int scale = std::clamp(blur * ch / 8, 1, 8);
  int radius = boxRadius(blur, ch, scale);
  // What lies within the blur's reach is drawn too, so the tile's edges
  // match its neighbours' - all but the last ninth of the reach, which
  // weighs too little to show and would widen the margin by a row.
  int reach = kBlurPasses * radius * scale * 8 / 9;
  int margin_rows = (reach + ch - 1) / ch, margin_cols = (reach + cw - 1) / cw;
  int r0 = std::max(0, row - margin_rows), r1 = std::min(below.rows(), row + rows + margin_rows);
  int c0 = std::max(0, col - margin_cols), c1 = std::min(below.cols(), col + cols + margin_cols);
  Fingerprint print;
  print.add(static_cast<uint64_t>(r0) << 32 | static_cast<uint32_t>(c0));
  print.add(static_cast<uint64_t>(r1) << 32 | static_cast<uint32_t>(c1));
  print.add(std::optional<Color>(default_fg_));
  print.add(std::optional<Color>(default_bg_));
  for (int r = r0; r < r1; r++) {
    for (int c = c0; c < c1; c++) {
      const CellBuffer::Cell & cell = below.at(r, c);
      if (cell.image > 0 && static_cast<size_t>(cell.image) <= placements_.size()) {
        const ImagePlacement & p = placements_[static_cast<size_t>(cell.image - 1)];
        print.add(p.key);
        print.add(p.image->version);
        print.add(static_cast<uint64_t>(r - p.row) << 32 | static_cast<uint32_t>(c - p.col));
        continue;
      }
      print.add(cell.text);
      print.add(cell.fg);
      print.add(cell.bg);
      print.add(static_cast<uint64_t>(cell.bold) | static_cast<uint64_t>(cell.continuation) << 1);
    }
  }
  BackdropTile * tile = nullptr;
  for (auto & t : backdrop_tiles_) {
    if (t.row == row && t.col == col && t.rows == rows && t.cols == cols) tile = &t;
  }
  if (!tile) {
    backdrop_tiles_.push_back(BackdropTile{row, col, rows, cols, 0, nextKey(), {}, nullptr, false});
    tile = &backdrop_tiles_.back();
  }
  tile->used = true;
  if (!tile->image || tile->hash != print.value() || tile->backdrop != backdrop) {
    Image drawn = rasterizer_->render(below, placements_, r0, c0, r1 - r0, c1 - c0, ch, cw, scale, default_fg_, default_bg_);
    filterPixels(drawn, backdrop, radius, backdrop.tint.value_or(default_bg_));
    // The tile's own pixels, and a pad of its neighbours' around them for
    // the display to blend the edges with as it stretches it.
    constexpr int kPad = 1;
    auto image = std::make_shared<Image>();
    int x0 = (col - c0) * cw / scale, x1 = std::min(drawn.width, (col + cols - c0) * cw / scale);
    int y0 = (row - r0) * ch / scale, y1 = std::min(drawn.height, (row + rows - r0) * ch / scale);
    image->width = std::max(1, x1 - x0) + 2 * kPad;
    image->height = std::max(1, y1 - y0) + 2 * kPad;
    image->pixels.assign(static_cast<size_t>(image->width) * static_cast<size_t>(image->height), Image::pack(0, 0, 0));
    for (int y = 0; y < image->height; y++) {
      int sy = std::clamp(y0 - kPad + y, 0, drawn.height - 1);
      for (int x = 0; x < image->width; x++) {
        int sx = std::clamp(x0 - kPad + x, 0, drawn.width - 1);
        image->pixels[static_cast<size_t>(y) * static_cast<size_t>(image->width) + static_cast<size_t>(x)] =
          drawn.pixels[static_cast<size_t>(sy) * static_cast<size_t>(drawn.width) + static_cast<size_t>(sx)];
      }
    }
    image->cols = cols;
    image->rows = rows;
    image->pad = kPad;
    image->version = nextImageVersion();
    tile->image = image;
    tile->hash = print.value();
    tile->backdrop = backdrop;
    redrawn_tiles_++;
  }
  ImagePlacement placement;
  placement.image = tile->image;
  placement.key = tile->key;
  placement.row = row;
  placement.col = col;
  placement.index = static_cast<int>(placements_.size());
  placements_.push_back(placement);
  return placement.index;
}

std::shared_ptr<const Image> BufferPlane::filteredImage(const ImagePlacement & placement, const Backdrop & backdrop) const {
  for (auto & f : filtered_images_) {
    if (f.key == placement.key && f.source_version == placement.image->version && f.backdrop == backdrop) {
      f.used = true;
      return f.image;
    }
  }
  auto image = std::make_shared<Image>(*placement.image);
  image->version = nextImageVersion();
  // The blur a cell backdrop gives, in pixels.
  filterPixels(*image, backdrop, boxRadius(backdrop.blur, std::max(1, cell_px_h_), 1), backdrop.tint.value_or(default_bg_));
  filtered_images_.push_back(FilteredImage{placement.key, placement.image->version, backdrop, image, true});
  return image;
}

void BufferPlane::compose(CellBuffer & out, int row, int col, int clip_row, int clip_col, int clip_rows, int clip_cols) const {
  if (!visible_) return;
  const BufferPlane & top = root();
  // The plane's own rectangle, inside the clip.
  int row_lo = std::max(clip_row, row), row_hi = std::min(clip_row + clip_rows, row + local_.rows());
  int col_lo = std::max(clip_col, col), col_hi = std::min(clip_col + clip_cols, col + local_.cols());
  // Backdrop cells show the layers below as they were before this plane.
  std::unique_ptr<BackdropSource> source;
  for (int r = row_lo; r < row_hi && !source; r++) {
    for (int c = col_lo; c < col_hi; c++) {
      if (!local_.at(r - row, c - col).backdrop.active()) continue;
      source = std::make_unique<BackdropSource>(out, top.placements_, top.cell_px_h_, top.cell_px_w_, top.default_fg_, top.default_bg_);
      break;
    }
  }
  std::map<int, Backdrop> filtered;  // the placements a backdrop covers
  // With pixels, a blurring backdrop is pictures in tiles of the screen.
  constexpr int kTileRows = 4, kTileCols = 16;
  std::map<std::pair<int, int>, int> tiles;  // tile origin -> placement index
  if (source && top.rasterizer_ && top.cell_px_h_ > 0 && top.cell_px_w_ > 0) {
    for (int tr = row_lo / kTileRows * kTileRows; tr < row_hi; tr += kTileRows) {
      for (int tc = col_lo / kTileCols * kTileCols; tc < col_hi; tc += kTileCols) {
        int r0 = std::max(tr, row_lo), r1 = std::min(tr + kTileRows, row_hi);
        int c0 = std::max(tc, col_lo), c1 = std::min(tc + kTileCols, col_hi);
        // A tile takes the backdrop of its first cell that blurs.
        const Backdrop * backdrop = nullptr;
        for (int r = r0; r < r1 && !backdrop; r++) {
          for (int c = c0; c < c1 && !backdrop; c++) {
            if (local_.at(r - row, c - col).backdrop.blur > 0) backdrop = &local_.at(r - row, c - col).backdrop;
          }
        }
        if (backdrop) tiles[{tr, tc}] = top.backdropTile(source->below(), r0, c0, r1 - r0, c1 - c0, *backdrop);
      }
    }
  }
  for (int r = row_lo; r < row_hi; r++) {
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
      CellBuffer::Cell below = out.at(r, c);
      CellBuffer::Cell put = cell;
      if (cell.backdrop.active()) {
        below = source->seen(r, c, cell.backdrop);
        CellBuffer::Cell own = cell;
        own.backdrop = {};
        if (CellBuffer::isBlank(own)) {
          auto tile = tiles.find({r / kTileRows * kTileRows, c / kTileCols * kTileCols});
          if (tile != tiles.end() && cell.backdrop.blur > 0) {
            CellBuffer::Cell marked;
            marked.image = tile->second + 1;
            out.put(r, c, marked);
            continue;
          }
          if (below.image) filtered.emplace(below.image - 1, cell.backdrop);
          out.put(r, c, below);
          continue;
        }
        put.backdrop = {};
        if (!put.bg) put.bg = below.bg;
      }
      if (cell.bg_blend) {
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
  for (auto & [index, backdrop] : filtered) {
    ImagePlacement & placement = top.placements_[static_cast<size_t>(index)];
    placement.image = top.filteredImage(placement, backdrop);
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
      for (int c = c_lo; c < c_hi; c++) out.put(r, c, marked);
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
  if (id == Key::Button1) {
    if (!release) return false;
    auto [py, px] = pk.plane->screenPosition();
    int row = input.getY() - py - 2, col = input.getX() - px;
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
