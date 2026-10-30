#include <cellui/CellRasterizer.h>
#include <cellui/Utf8.h>

#include <canvas/Context.h>
#include "NativeContext.h"

#ifndef __APPLE__
#include <fontconfig/fontconfig.h>
#endif

#include <algorithm>
#include <cmath>
#include <map>

namespace cellui {

namespace {

uint32_t firstCodepoint(const std::string & s) {
  if (s.empty()) return 0;
  auto b = [&](size_t i) { return i < s.size() ? static_cast<uint32_t>(static_cast<unsigned char>(s[i])) : 0u; };
  uint32_t c = b(0);
  if (c < 0x80) return c;
  if ((c >> 5) == 6) return ((c & 0x1F) << 6) | (b(1) & 0x3F);
  if ((c >> 4) == 14) return ((c & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F);
  return ((c & 0x07) << 18) | ((b(1) & 0x3F) << 12) | ((b(2) & 0x3F) << 6) | (b(3) & 0x3F);
}

#ifdef __APPLE__
// Core Text draws a character the font lacks with another font by itself.
class FontFallback {
 public:
  explicit FontFallback(std::string) { }
  const std::string & familyFor(uint32_t) {
    static const std::string kPrimary;
    return kPrimary;
  }
};
#else
// Which font draws a character. The text drawing has no fallback of its own,
// so a character the font asked for lacks (a musical accidental, say) is drawn
// with the best installed font that has it, asked of fontconfig.
class FontFallback {
 public:
  explicit FontFallback(std::string family) : family_(std::move(family)) {
    FcInit();
    primary_ = charsetOf(family_, 0);
  }
  ~FontFallback() {
    if (primary_) FcCharSetDestroy(primary_);
  }
  FontFallback(const FontFallback &) = delete;
  FontFallback & operator=(const FontFallback &) = delete;

  // The family to draw `cp` with: empty for the one asked for.
  const std::string & familyFor(uint32_t cp) {
    static const std::string kPrimary;
    if (cp < 0x80 || !primary_ || FcCharSetHasChar(primary_, cp)) return kPrimary;
    auto found = cache_.find(cp);
    if (found == cache_.end()) found = cache_.emplace(cp, search(cp)).first;
    return found->second;
  }

 private:
  // The best installed font for `family` that has `cp` (any, when 0).
  FcPattern * match(const std::string & family, uint32_t cp) const {
    FcPattern * pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(family.c_str()));
    if (cp) {
      FcCharSet * wanted = FcCharSetCreate();
      FcCharSetAddChar(wanted, cp);
      FcPatternAddCharSet(pattern, FC_CHARSET, wanted);
      FcCharSetDestroy(wanted);
    }
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    FcPattern * best = FcFontMatch(nullptr, pattern, &result);
    FcPatternDestroy(pattern);
    return best;
  }
  FcCharSet * charsetOf(const std::string & family, uint32_t cp) const {
    FcPattern * best = match(family, cp);
    if (!best) return nullptr;
    FcCharSet * set = nullptr;
    FcCharSet * own = nullptr;
    if (FcPatternGetCharSet(best, FC_CHARSET, 0, &own) == FcResultMatch) set = FcCharSetCopy(own);
    FcPatternDestroy(best);
    return set;
  }
  std::string search(uint32_t cp) const {
    std::string name;
    FcPattern * best = match(family_, cp);
    if (!best) return name;
    FcChar8 * family = nullptr;
    FcCharSet * set = nullptr;
    if (FcPatternGetCharSet(best, FC_CHARSET, 0, &set) == FcResultMatch && FcCharSetHasChar(set, cp) &&
        FcPatternGetString(best, FC_FAMILY, 0, &family) == FcResultMatch)
      name = reinterpret_cast<const char *>(family);
    FcPatternDestroy(best);
    return name;
  }

  std::string family_;
  FcCharSet * primary_ = nullptr;
  std::map<uint32_t, std::string> cache_;
};
#endif

// An opaque RGB picture that rectangles are drawn into with their exact
// coverage of each pixel, so shapes smaller than a pixel still count.
class Pixels {
 public:
  Pixels(int width, int height, const Color & fill)
    : width_(width), height_(height),
      rgb_(static_cast<size_t>(width) * static_cast<size_t>(height) * 3) {
    auto rgb = fill.toSrgb8();
    for (size_t i = 0; i < rgb_.size(); i += 3) {
      rgb_[i] = static_cast<float>(rgb[0]);
      rgb_[i + 1] = static_cast<float>(rgb[1]);
      rgb_[i + 2] = static_cast<float>(rgb[2]);
    }
  }

  // A rectangle in pixels, `alpha` of `color` over what is there.
  void fill(double x, double y, double w, double h, const Color & color, float alpha = 1.0f) {
    int px0 = std::max(0, static_cast<int>(std::floor(x))), px1 = std::min(width_, static_cast<int>(std::ceil(x + w)));
    int py0 = std::max(0, static_cast<int>(std::floor(y))), py1 = std::min(height_, static_cast<int>(std::ceil(y + h)));
    auto rgb = color.toSrgb8();
    for (int py = py0; py < py1; py++) {
      double cover_y = std::min(y + h, py + 1.0) - std::max(y, static_cast<double>(py));
      for (int px = px0; px < px1; px++) {
        double cover_x = std::min(x + w, px + 1.0) - std::max(x, static_cast<double>(px));
        blendRgb(px, py, rgb, static_cast<float>(cover_x * cover_y) * alpha);
      }
    }
  }
  // One pixel, `alpha` of `color`.
  void blend(int px, int py, const Color & color, float alpha) { blendRgb(px, py, color.toSrgb8(), alpha); }
  // The same with the colour already as 8-bit sRGB. The picture holds
  // encoded values: a shape's edge coverage mixes in that space.
  void blendRgb(int px, int py, const std::array<uint8_t, 3> & rgb, float alpha) {
    if (alpha <= 0.0f) return;
    float * p = &rgb_[(static_cast<size_t>(py) * static_cast<size_t>(width_) + static_cast<size_t>(px)) * 3];
    p[0] += (static_cast<float>(rgb[0]) - p[0]) * alpha;
    p[1] += (static_cast<float>(rgb[1]) - p[1]) * alpha;
    p[2] += (static_cast<float>(rgb[2]) - p[2]) * alpha;
  }
  // As canvas reads an image: bytes R, G, B, A.
  std::vector<unsigned char> rgba() const {
    std::vector<unsigned char> out(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4);
    for (size_t i = 0, j = 0; i < rgb_.size(); i += 3, j += 4) {
      for (size_t k = 0; k < 3; k++) out[j + k] = static_cast<unsigned char>(std::clamp(rgb_[i + k] + 0.5f, 0.0f, 255.0f));
      out[j + 3] = 255;
    }
    return out;
  }

 private:
  int width_, height_;
  std::vector<float> rgb_;
};

class CanvasRasterizer : public CellRasterizer {
 public:
  CanvasRasterizer(std::string family, bool cell_shapes) : family_(family), fonts_(std::move(family)), cell_shapes_(cell_shapes) { }

  Image render(const CellBuffer & cells, const std::vector<ImagePlacement> & images, int row, int col, int rows, int cols, int cell_h,
               int cell_w, int scale, Color default_fg, Color default_bg) override {
    scale = std::max(1, scale);
    Image out;
    out.width = std::max(1, cols * cell_w / scale);
    out.height = std::max(1, rows * cell_h / scale);
    out.pixels.assign(static_cast<size_t>(out.width) * static_cast<size_t>(out.height), Image::pack(0, 0, 0));
    // Everything but text is drawn here, in pixels of the smaller picture.
    const double s = scale, w = cell_w / s, h = cell_h / s;
    Pixels px(out.width, out.height, default_bg);
    int r_lo = std::max(row, 0), r_hi = std::min(row + rows, cells.rows());
    int c_lo = std::max(col, 0), c_hi = std::min(col + cols, cells.cols());
    bool any_text = false;
    for (int r = r_lo; r < r_hi; r++) {
      for (int c = c_lo; c < c_hi; c++) {
        const CellBuffer::Cell & cell = cells.at(r, c);
        double x = (c - col) * w, y = (r - row) * h;
        if (cell.image > 0) {
          if (static_cast<size_t>(cell.image) <= images.size()) drawImageCell(px, images[static_cast<size_t>(cell.image - 1)], r, c, x, y, w, h, s, cell_h, cell_w);
          continue;
        }
        if (cell.continuation) continue;
        if (cell.bg) px.fill(x, y, w, h, *cell.bg);
        if (!drawShape(px, cell, x, y, w, h, cell.fg.value_or(default_fg))) any_text = true;
      }
    }
    auto rgba = px.rgba();
    if (any_text) drawText(rgba, cells, row, col, r_lo, r_hi, c_lo, c_hi, out.width, out.height, s, cell_h, cell_w, default_fg);
    for (size_t i = 0; i < out.pixels.size(); i++) out.pixels[i] = Image::pack(rgba[4 * i], rgba[4 * i + 1], rgba[4 * i + 2]);
    return out;
  }


  Image renderText(const CellBuffer & cells, int row, int col, int rows, int cols, int cell_h, int cell_w, Color default_fg) override {
    Image out;
    out.width = std::max(1, cols * cell_w);
    out.height = std::max(1, rows * cell_h);
    auto context = cellui::nativeContext(static_cast<unsigned>(out.width), static_cast<unsigned>(out.height));
    if (!context) return {};
    canvas::Context & ctx = *context;
    int r_lo = std::max(row, 0), r_hi = std::min(row + rows, cells.rows());
    int c_lo = std::max(col, 0), c_hi = std::min(col + cols, cells.cols());
    drawTextRuns(ctx, cells, row, col, r_lo, r_hi, c_lo, c_hi, 1.0, cell_h, cell_w, default_fg);
    // The surface's own layout is premultiplied bytes B, G, R, A.
    auto result = ctx.getDefaultSurface().createImage(1.0f);
    const canvas::ImageData & data = result->getData();
    if (data.getNumChannels() != 4 || static_cast<int>(data.getWidth()) < out.width || static_cast<int>(data.getHeight()) < out.height) return {};
    const unsigned char * src = data.getData();
    const size_t stride = static_cast<size_t>(data.getWidth()) * 4;
    out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
    for (int y = 0; y < out.height; y++) {
      for (int x = 0; x < out.width; x++) {
        const unsigned char * p = src + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
        out.pixels[static_cast<size_t>(y) * static_cast<size_t>(out.width) + static_cast<size_t>(x)] = Image::pack(p[2], p[1], p[0], p[3]);
      }
    }
    return out;
  }

 private:
  // The part of a picture one cell shows, sampled at each pixel's centre.
  static void drawImageCell(Pixels & px, const ImagePlacement & p, int r, int c, double x, double y, double w, double h, double s,
                            int cell_h, int cell_w) {
    const Image & img = *p.image;
    if (img.width <= 0 || img.height <= 0) return;
    int ix0, ix1, iy0, iy1;
    img.pixelSpan(c - p.col, 1, cell_w, true, ix0, ix1);
    img.pixelSpan(r - p.row, 1, cell_h, false, iy0, iy1);
    if (ix1 <= ix0 || iy1 <= iy0) return;
    (void)s;
    for (int py = static_cast<int>(std::floor(y)); py < static_cast<int>(std::ceil(y + h)); py++) {
      for (int pxl = static_cast<int>(std::floor(x)); pxl < static_cast<int>(std::ceil(x + w)); pxl++) {
        double u = std::clamp((pxl + 0.5 - x) / w, 0.0, 0.999), v = std::clamp((py + 0.5 - y) / h, 0.0, 0.999);
        int sx = ix0 + static_cast<int>(u * (ix1 - ix0)), sy = iy0 + static_cast<int>(v * (iy1 - iy0));
        uint32_t src = img.pixels[static_cast<size_t>(sy) * static_cast<size_t>(img.width) + static_cast<size_t>(sx)];
        px.fill(pxl, py, 1, 1, Color(Image::red(src), Image::green(src), Image::blue(src)), Image::alpha(src) / 255.0f);
      }
    }
  }

  // Block elements and braille, drawn as shapes, exactly, when the cells hold
  // a terminal's fallback charts (a font may lack them, and a meter is made
  // of them); otherwise they are text like any glyph. False for a glyph that
  // is text.
  bool drawShape(Pixels & px, const CellBuffer::Cell & cell, double x, double y, double w, double h, const Color & fg) {
    uint32_t cp = firstCodepoint(cell.text);
    if (cp == 0 || cp == ' ') return true;
    if (!cell_shapes_) return false;
    if (cp == 0x2588) {
      px.fill(x, y, w, h, fg);
    } else if (cp >= 0x2581 && cp <= 0x2587) {  // lower eighths
      double part = h * (cp - 0x2580) / 8.0;
      px.fill(x, y + h - part, w, part, fg);
    } else if (cp >= 0x2589 && cp <= 0x258F) {  // left eighths
      px.fill(x, y, w * (0x2590 - cp) / 8.0, h, fg);
    } else if (cp == 0x2580) {
      px.fill(x, y, w, h / 2, fg);
    } else if (cp == 0x2590) {
      px.fill(x + w / 2, y, w / 2, h, fg);
    } else if (cp >= 0x2591 && cp <= 0x2593) {  // shades
      px.fill(x, y, w, h, fg, static_cast<float>(cp - 0x2590) / 4.0f);
    } else if (cp >= 0x2800 && cp <= 0x28FF) {  // braille: dots 1-3 and 7 left, 4-6 and 8 right
      static const int kDotRow[8] = {0, 1, 2, 0, 1, 2, 3, 3}, kDotCol[8] = {0, 0, 0, 1, 1, 1, 0, 1};
      uint32_t bits = cp - 0x2800;
      for (int d = 0; d < 8; d++) {
        if (!(bits & (1u << d))) continue;
        px.fill(x + w * (0.25 + 0.5 * kDotCol[d]) - w / 8, y + h * (0.125 + 0.25 * kDotRow[d]) - h / 16, w / 4, h / 8, fg);
      }
    } else {
      return false;
    }
    return true;
  }

  // The text, a cell at a time in runs of one colour and weight, over the picture,
  // in the system's font sized so that its advance is a cell's width.
  void drawText(std::vector<unsigned char> & rgba, const CellBuffer & cells, int row, int col, int r_lo, int r_hi, int c_lo, int c_hi,
                int width, int height, double s, int cell_h, int cell_w, const Color & default_fg) {
    auto context = cellui::nativeContext(static_cast<unsigned>(width), static_cast<unsigned>(height));
    canvas::Context & ctx = *context;
    canvas::ImageData base(rgba.data(), static_cast<unsigned>(width), static_cast<unsigned>(height), 4);
    ctx.drawImage(base, 0, 0, width, height);
    drawTextRuns(ctx, cells, row, col, r_lo, r_hi, c_lo, c_hi, s, cell_h, cell_w, default_fg);
    // The surface's own layout is bytes B, G, R, A (opaque here).
    auto result = ctx.getDefaultSurface().createImage(1.0f);
    const canvas::ImageData & data = result->getData();
    if (data.getNumChannels() != 4 || static_cast<int>(data.getWidth()) != width || static_cast<int>(data.getHeight()) != height) return;
    const unsigned char * src = data.getData();
    for (size_t i = 0; i < rgba.size(); i += 4) {
      rgba[i] = src[i + 2];
      rgba[i + 1] = src[i + 1];
      rgba[i + 2] = src[i];
    }
  }


  // The glyphs and underlines of the cells, drawn on `ctx` over whatever it holds.
  void drawTextRuns(canvas::Context & ctx, const CellBuffer & cells, int row, int col, int r_lo, int r_hi, int c_lo, int c_hi, double s,
                    int cell_h, int cell_w, const Color & default_fg) {
    ctx.scale(1.0 / s, 1.0 / s);
    ctx.font.family = family_;
    ctx.font.size = fontSize(ctx, cell_h, cell_w);
    ctx.textBaseline = canvas::TextBaseline::MIDDLE;
    for (int r = r_lo; r < r_hi; r++) {
      for (int c = c_lo; c < c_hi;) {
        const CellBuffer::Cell & first = cells.at(r, c);
        Pixels none(1, 1, Color());
        if (first.image > 0 || first.continuation || drawShape(none, first, 0, 0, 1, 1, Color())) {
          c++;
          continue;
        }
        Color fg = first.fg.value_or(default_fg);
        auto fg8 = fg.toSrgb8();
        ctx.fillStyle = canvas::Color(fg8[0] / 255.0f, fg8[1] / 255.0f, fg8[2] / 255.0f, 1.0f);
        ctx.font.weight = first.bold ? "bold" : "normal";
        ctx.font.style = first.italic ? canvas::Font::ITALIC : canvas::Font::NORMAL_STYLE;
        auto draw = [&](const std::string & text, int at, const std::string & family) {
          if (text.empty()) return;
          ctx.font.family = family;
          ctx.fillText(text, (at - col) * cell_w, (r - row) * cell_h + cell_h / 2.0);
        };
        for (; c < c_hi; c++) {
          const CellBuffer::Cell & cell = cells.at(r, c);
          if (cell.continuation) continue;
          Color cell_fg = cell.fg.value_or(default_fg);
          if (cell.image > 0 || cell.bold != first.bold || cell.italic != first.italic || cell_fg.toSrgb8() != fg8 || drawShape(none, cell, 0, 0, 1, 1, Color()))
            break;
          const std::string & other = fonts_.familyFor(firstCodepoint(cell.text));
          if (!other.empty()) {
            // A character of another font stands alone in its cell.
            // Shrunk to its cell if its font draws it wider.
            float size = ctx.font.size;
            ctx.font.family = other;
            double glyph_width = ctx.measureText(cell.text).width;
            int cells_wide = std::max(1, utf8::displayWidth(cell.text));
            if (glyph_width > cells_wide * cell_w) ctx.font.size = static_cast<float>(size * cells_wide * cell_w / glyph_width);
            draw(cell.text, c, other);
            ctx.font.size = size;
            continue;
          }
          // One cell at a time, so the glyphs sit on the grid whatever the
          // font's advances are (a space is not always as wide as a letter).
          if (cell.text != " ") draw(cell.text, c, family_);
          if (cell.underline) {
            double thickness = std::max(1.0, cell_h / 14.0);
            ctx.fillRect((c - col) * cell_w, (r - row) * cell_h + cell_h - 2.0 * thickness, cell_w, thickness);
          }
        }
      }
    }
  }

  // The size at which the font's advance is a cell's width (and its height
  // fits the cell), measured once per cell size.
  float fontSize(canvas::Context & ctx, int cell_h, int cell_w) {
    if (cell_h != measured_h_ || cell_w != measured_w_) {
      measured_h_ = cell_h;
      measured_w_ = cell_w;
      ctx.font.size = 100.0f;
      double advance = ctx.measureText("MMMMMMMMMM").width / 10.0;
      font_size_ = static_cast<float>(cell_h) * 0.8f;
      if (advance > 0) font_size_ = std::min(font_size_, static_cast<float>(100.0 * cell_w / advance));
    }
    return font_size_;
  }

  std::string family_;
  FontFallback fonts_;
  bool cell_shapes_;
  int measured_h_ = 0, measured_w_ = 0;
  float font_size_ = 0;
};

}  // namespace

std::unique_ptr<CellRasterizer> canvasRasterizer(const std::string & family, bool cell_shapes) { return std::make_unique<CanvasRasterizer>(family, cell_shapes); }

}  // namespace cellui
