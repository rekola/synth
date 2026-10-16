#include <cellui/term/BackdropEmulation.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace cellui::term {

namespace {

// What cells without a colour are drawn in, when the display does not say.
Color defaultFg(const DisplayInfo & display) { return display.default_fg.value_or(Color(255, 255, 255)); }
Color defaultBg(const DisplayInfo & display) { return display.default_bg.value_or(Color(0, 0, 0)); }

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

BackdropEmulation::BackdropEmulation(std::shared_ptr<CellRasterizer> rasterizer) : rasterizer_(std::move(rasterizer)) { }

int BackdropEmulation::tile(const CellBuffer & below, std::vector<ImagePlacement> & images, int row, int col, int rows, int cols, const Backdrop & backdrop,
                            const DisplayInfo & display) {
  const Color default_fg = defaultFg(display), default_bg = defaultBg(display);
  int blur = std::min(backdrop.blur, Backdrop::kMaxBlur);
  // Drawn smaller than the display shows it: a blur needs no more detail.
  int ch = display.cell_px_height, cw = display.cell_px_width;
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
  print.add(std::optional<Color>(default_fg));
  print.add(std::optional<Color>(default_bg));
  for (int r = r0; r < r1; r++) {
    for (int c = c0; c < c1; c++) {
      const CellBuffer::Cell & cell = below.at(r, c);
      if (cell.image > 0 && static_cast<size_t>(cell.image) <= images.size()) {
        const ImagePlacement & p = images[static_cast<size_t>(cell.image - 1)];
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
  Tile * tile = nullptr;
  for (auto & t : tiles_) {
    if (t.row == row && t.col == col && t.rows == rows && t.cols == cols) tile = &t;
  }
  if (!tile) {
    tiles_.push_back(Tile{row, col, rows, cols, 0, next_key_++, {}, nullptr, false});
    tile = &tiles_.back();
  }
  tile->used = true;
  if (!tile->image || tile->hash != print.value() || tile->backdrop != backdrop) {
    Image drawn = rasterizer_->render(below, images, r0, c0, r1 - r0, c1 - c0, ch, cw, scale, default_fg, default_bg);
    filterPixels(drawn, backdrop, radius, backdrop.tint.value_or(default_bg));
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
    image->version = next_version_++;
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
  placement.index = static_cast<int>(images.size());
  images.push_back(placement);
  return placement.index;
}

std::shared_ptr<const Image> BackdropEmulation::filtered(const ImagePlacement & placement, const Backdrop & backdrop, const DisplayInfo & display) {
  for (auto & f : filtered_) {
    if (f.key == placement.key && f.source_version == placement.image->version && f.backdrop == backdrop) {
      f.used = true;
      return f.image;
    }
  }
  auto image = std::make_shared<Image>(*placement.image);
  image->version = next_version_++;
  // The blur a cell backdrop gives, in pixels.
  filterPixels(*image, backdrop, boxRadius(backdrop.blur, std::max(1, display.cell_px_height), 1), backdrop.tint.value_or(defaultBg(display)));
  filtered_.push_back(Filtered{placement.key, placement.image->version, backdrop, image, true});
  return image;
}

void BackdropEmulation::applyLayer(CellBuffer & out, std::vector<ImagePlacement> & images, const BackdropLayer & layer,
                                   const DisplayInfo & display) {
  const Backdrop & backdrop = layer.backdrop;
  int row_lo = std::max(0, layer.row), row_hi = std::min(out.rows(), layer.row + layer.rows);
  int col_lo = std::max(0, layer.col), col_hi = std::min(out.cols(), layer.col + layer.cols);
  if (row_lo >= row_hi || col_lo >= col_hi) return;
  // The layers below, as they were before the backdrop.
  BackdropSource source(out, images, display.cell_px_height, display.cell_px_width, defaultFg(display), defaultBg(display));

  const CellBuffer & below = source.below();
  // With pictures, a blurring backdrop is pictures in tiles of the screen.
  constexpr int kTileRows = 4, kTileCols = 16;
  std::map<std::pair<int, int>, int> tiles;  // tile origin -> placement index
  if (backdrop.blur > 0 && rasterizer_ && display.cell_px_height > 0 && display.cell_px_width > 0) {
    for (int tr = row_lo / kTileRows * kTileRows; tr < row_hi; tr += kTileRows) {
      for (int tc = col_lo / kTileCols * kTileCols; tc < col_hi; tc += kTileCols) {
        int r0 = std::max(tr, row_lo), r1 = std::min(tr + kTileRows, row_hi);
        int c0 = std::max(tc, col_lo), c1 = std::min(tc + kTileCols, col_hi);
        tiles[{tr, tc}] = tile(below, images, r0, c0, r1 - r0, c1 - c0, backdrop, display);
      }
    }
  }
  std::map<int, Backdrop> filtered_placements;  // the placements a backdrop covers
  for (int r = row_lo; r < row_hi; r++) {
    for (int c = col_lo; c < col_hi; c++) {
      auto found = tiles.find({r / kTileRows * kTileRows, c / kTileCols * kTileCols});
      if (found != tiles.end()) {
        CellBuffer::Cell marked;
        marked.image = found->second + 1;
        out.put(r, c, marked);
        continue;
      }
      CellBuffer::Cell shown = source.seen(r, c, backdrop);
      if (shown.image) filtered_placements.emplace(shown.image - 1, backdrop);
      out.put(r, c, shown);
    }
  }
  for (auto & [index, filtered_backdrop] : filtered_placements) {
    ImagePlacement & placement = images[static_cast<size_t>(index)];
    placement.image = filtered(placement, filtered_backdrop, display);
  }
}

void BackdropEmulation::apply(CellBuffer & frame, std::vector<ImagePlacement> & images, const std::vector<BackdropLayer> & layers,
                              const DisplayInfo & display) {
  for (auto & f : filtered_) f.used = false;
  for (auto & t : tiles_) t.used = false;
  redrawn_tiles_ = 0;
  if (!layers.empty()) {
    if (!rasterizer_ && display.pictures) rasterizer_ = canvasRasterizer();
    // What is on top of what: each layer's own cells over the stack so far,
    // its backdrop on the stack, the frame's cells last.
    auto over = [](CellBuffer & out, const CellBuffer & cells) {
      for (int r = 0; r < std::min(out.rows(), cells.rows()); r++) {
        for (int c = 0; c < std::min(out.cols(), cells.cols()); c++) {
          if (!CellBuffer::isBlank(cells.at(r, c))) out.put(r, c, cells.at(r, c));
        }
      }
    };
    CellBuffer out = layers[0].cells;
    for (size_t i = 0; i < layers.size(); i++) {
      if (i > 0) over(out, layers[i].cells);
      applyLayer(out, images, layers[i], display);
    }
    over(out, frame);
    frame = std::move(out);
  }
  tiles_.erase(std::remove_if(tiles_.begin(), tiles_.end(), [](const Tile & t) { return !t.used; }), tiles_.end());
  filtered_.erase(std::remove_if(filtered_.begin(), filtered_.end(), [](const Filtered & f) { return !f.used; }), filtered_.end());
}

}  // namespace cellui::term
