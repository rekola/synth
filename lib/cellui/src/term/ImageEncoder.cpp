#include <cellui/term/ImageEncoder.h>

#include <cellui/term/FrameEncoder.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>

namespace cellui::term {

namespace {

constexpr int kCubeLevels[6] = {0, 95, 135, 175, 215, 255};
// The kitty protocol's largest chunk of base64 data.
constexpr size_t kKittyChunk = 4096;

std::string base64(const std::vector<uint8_t> & data) {
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    uint32_t v = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
    out += table[(v >> 18) & 63];
    out += table[(v >> 12) & 63];
    out += table[(v >> 6) & 63];
    out += table[v & 63];
  }
  if (i < data.size()) {
    uint32_t v = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < data.size()) v |= static_cast<uint32_t>(data[i + 1]) << 8;
    out += table[(v >> 18) & 63];
    out += table[(v >> 12) & 63];
    out += i + 1 < data.size() ? table[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

std::string cup(int row, int col) { return "\x1b[" + std::to_string(row + 1) + ";" + std::to_string(col + 1) + "H"; }

void paletteRgb(int index, int & r, int & g, int & b) {
  if (index >= 232) {
    r = g = b = 8 + 10 * (index - 232);
  } else {
    int i = index - 16;
    r = kCubeLevels[i / 36];
    g = kCubeLevels[(i / 6) % 6];
    b = kCubeLevels[i % 6];
  }
}

int percent(int v) { return static_cast<int>(std::lround(v * 100.0 / 255.0)); }

}  // namespace

ImageEncoder::Profile ImageEncoder::Profile::from(const Capabilities & caps) {
  Profile p;
  // iTerm2 also draws Sixel, until its own images are encoded.
  p.protocol = caps.pixels == PixelProtocol::ITERM2 ? PixelProtocol::SIXEL : caps.pixels;
  p.cell_px_height = caps.cell_px_height;
  p.cell_px_width = caps.cell_px_width;
  p.sixel_setup = caps.sixel_setup;
  return p;
}

std::string encodeSixel(const Image & image, int x0, int y0, int width, int height, const std::vector<bool> & keep) {
  // Each pixel's register (its palette index - 16), or -1 when left out.
  std::vector<int> regs(static_cast<size_t>(width) * static_cast<size_t>(height), -1);
  std::unordered_map<uint32_t, int> cache;
  std::set<int> used;
  bool transparent = height % 6 != 0;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      size_t i = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
      uint32_t p = image.pixels[static_cast<size_t>(y0 + y) * static_cast<size_t>(image.width) + static_cast<size_t>(x0 + x)];
      if (!keep[i] || Image::alpha(p) < 192) {
        transparent = true;
        continue;
      }
      uint32_t rgb = p & 0x00ffffffu;
      auto it = cache.find(rgb);
      int reg;
      if (it != cache.end()) {
        reg = it->second;
      } else {
        reg = nearest256(Color(Image::red(p), Image::green(p), Image::blue(p))) - 16;
        cache.emplace(rgb, reg);
      }
      regs[i] = reg;
      used.insert(reg);
    }
  }
  std::string out = "\x1bP0;" + std::string(transparent ? "1" : "0") + ";0q\"1;1;" + std::to_string(width) + ";" + std::to_string(height);
  for (int reg : used) {
    int r, g, b;
    paletteRgb(reg + 16, r, g, b);
    out += "#" + std::to_string(reg) + ";2;" + std::to_string(percent(r)) + ";" + std::to_string(percent(g)) + ";" + std::to_string(percent(b));
  }
  std::vector<int> bits(static_cast<size_t>(width));
  for (int band = 0; band < height; band += 6) {
    std::set<int> in_band;
    for (int y = band; y < std::min(band + 6, height); y++) {
      for (int x = 0; x < width; x++) {
        int reg = regs[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
        if (reg >= 0) in_band.insert(reg);
      }
    }
    bool first = true;
    for (int reg : in_band) {
      std::fill(bits.begin(), bits.end(), 0);
      for (int y = band; y < std::min(band + 6, height); y++) {
        for (int x = 0; x < width; x++) {
          if (regs[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] == reg) bits[static_cast<size_t>(x)] |= 1 << (y - band);
        }
      }
      // Trailing empty columns need not be sent.
      int last = width - 1;
      while (last >= 0 && bits[static_cast<size_t>(last)] == 0) last--;
      if (!first) out += '$';
      first = false;
      out += "#" + std::to_string(reg);
      for (int x = 0; x <= last;) {
        int run = 1;
        while (x + run <= last && bits[static_cast<size_t>(x + run)] == bits[static_cast<size_t>(x)]) run++;
        char c = static_cast<char>(0x3f + bits[static_cast<size_t>(x)]);
        if (run > 2) out += "!" + std::to_string(run) + c;
        else out.append(static_cast<size_t>(run), c);
        x += run;
      }
    }
    out += '-';
  }
  out += "\x1b\\";
  return out;
}

std::vector<ImageEncoder::Rect> ImageEncoder::visibleRects(const CellBuffer & frame, const ImagePlacement & p, bool avoid_last_row) const {
  std::vector<Rect> rects;
  int ch = profile_.cell_px_height, cw = profile_.cell_px_width;
  if (ch <= 0 || cw <= 0) return rects;
  int rows = (p.image->height + ch - 1) / ch, cols = (p.image->width + cw - 1) / cw;
  int row_hi = std::min(p.row + rows, frame.rows() - (avoid_last_row ? 1 : 0));
  int col_hi = std::min(p.col + cols, frame.cols());
  for (int r = std::max(p.row, 0); r < row_hi; r++) {
    for (int c = std::max(p.col, 0); c < col_hi;) {
      if (frame.at(r, c).image != p.index + 1) {
        c++;
        continue;
      }
      int start = c;
      while (c < col_hi && frame.at(r, c).image == p.index + 1) c++;
      // A run under the same run of the row above extends its rectangle.
      bool merged = false;
      for (Rect & rect : rects) {
        if (rect.col == start && rect.cols == c - start && rect.row + rect.rows == r) {
          rect.rows++;
          merged = true;
          break;
        }
      }
      if (!merged) rects.push_back(Rect{r, start, 1, c - start});
    }
  }
  return rects;
}

std::string ImageEncoder::kitty(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared) {
  std::string out;
  const Image & img = *p.image;
  uint32_t old_id = shown.id;
  bool changed = fresh || shown.version != img.version;
  if (changed) {
    // A new picture goes up under a new id; the old one stays on screen
    // until the new one is placed.
    if (++next_id_ == 0) next_id_ = 1;
    shown.id = next_id_;
    shown.version = img.version;
    std::vector<uint8_t> rgb;
    rgb.reserve(img.pixels.size() * 3);
    for (uint32_t px : img.pixels) {
      rgb.push_back(Image::red(px));
      rgb.push_back(Image::green(px));
      rgb.push_back(Image::blue(px));
    }
    std::string data = base64(rgb);
    for (size_t i = 0; i < data.size() || i == 0; i += kKittyChunk) {
      bool more = i + kKittyChunk < data.size();
      out += "\x1b_G";
      if (i == 0) out += "a=t,f=24,s=" + std::to_string(img.width) + ",v=" + std::to_string(img.height) + ",i=" + std::to_string(shown.id) + ",";
      out += "q=2,m=" + std::string(more ? "1" : "0") + ";";
      out += data.substr(i, kKittyChunk);
      out += "\x1b\\";
      if (data.empty()) break;
    }
  }
  std::vector<Rect> rects = visibleRects(frame, p, false);
  if (changed || cleared || rects != shown.rects || p.row != shown.row || p.col != shown.col) {
    int ch = profile_.cell_px_height, cw = profile_.cell_px_width;
    for (size_t k = 0; k < rects.size(); k++) {
      const Rect & r = rects[k];
      int x = (r.col - p.col) * cw, y = (r.row - p.row) * ch;
      int w = std::min(r.cols * cw, img.width - x), h = std::min(r.rows * ch, img.height - y);
      if (w <= 0 || h <= 0) continue;
      out += cup(r.row, r.col);
      out += "\x1b_Ga=p,i=" + std::to_string(shown.id) + ",p=" + std::to_string(k + 1) + ",x=" + std::to_string(x) +
             ",y=" + std::to_string(y) + ",w=" + std::to_string(w) + ",h=" + std::to_string(h) + ",C=1,q=2\x1b\\";
    }
    // Placements this picture no longer has.
    if (!changed && !cleared) {
      for (size_t k = rects.size(); k < shown.rects.size(); k++) {
        out += "\x1b_Ga=d,d=i,i=" + std::to_string(shown.id) + ",p=" + std::to_string(k + 1) + ",q=2\x1b\\";
      }
    }
    shown.rects = rects;
    shown.row = p.row;
    shown.col = p.col;
  }
  if (changed && old_id) out += "\x1b_Ga=d,d=I,i=" + std::to_string(old_id) + ",q=2\x1b\\";
  return out;
}

std::string ImageEncoder::sixel(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared) {
  const Image & img = *p.image;
  std::vector<Rect> rects = visibleRects(frame, p, true);
  bool changed = fresh || cleared || shown.version != img.version || rects != shown.rects || p.row != shown.row || p.col != shown.col;
  shown.version = img.version;
  shown.rects = rects;
  shown.row = p.row;
  shown.col = p.col;
  if (!changed || rects.empty()) return "";
  int ch = profile_.cell_px_height, cw = profile_.cell_px_width;
  int r0 = rects[0].row, c0 = rects[0].col, r1 = r0, c1 = c0;
  for (const Rect & r : rects) {
    r0 = std::min(r0, r.row);
    c0 = std::min(c0, r.col);
    r1 = std::max(r1, r.row + r.rows);
    c1 = std::max(c1, r.col + r.cols);
  }
  int x = (c0 - p.col) * cw, y = (r0 - p.row) * ch;
  int w = std::min((c1 - c0) * cw, img.width - x), h = std::min((r1 - r0) * ch, img.height - y);
  if (w <= 0 || h <= 0) return "";
  std::vector<bool> keep(static_cast<size_t>(w) * static_cast<size_t>(h), false);
  for (const Rect & r : rects) {
    for (int py = (r.row - r0) * ch; py < std::min((r.row - r0 + r.rows) * ch, h); py++) {
      for (int px = (r.col - c0) * cw; px < std::min((r.col - c0 + r.cols) * cw, w); px++) {
        keep[static_cast<size_t>(py) * static_cast<size_t>(w) + static_cast<size_t>(px)] = true;
      }
    }
  }
  std::string out;
  if (!sixel_ready_) {
    out += profile_.sixel_setup;
    sixel_ready_ = true;
  }
  out += cup(r0, c0);
  out += encodeSixel(img, x, y, w, h, keep);
  return out;
}

std::string ImageEncoder::encode(const CellBuffer & frame, const std::vector<ImagePlacement> & images, bool cleared) {
  std::string out;
  if (profile_.protocol == PixelProtocol::NONE) return out;
  if (cleared) {
    for (auto & [key, shown] : shown_) shown.rects.clear();
  }
  std::set<uint64_t> seen;
  for (const ImagePlacement & p : images) {
    if (!p.image || p.image->width <= 0 || p.image->height <= 0) continue;
    seen.insert(p.key);
    auto it = shown_.find(p.key);
    bool fresh = it == shown_.end();
    Shown & shown = shown_[p.key];
    out += profile_.protocol == PixelProtocol::KITTY ? kitty(frame, p, shown, fresh, cleared) : sixel(frame, p, shown, fresh, cleared);
  }
  for (auto it = shown_.begin(); it != shown_.end();) {
    if (seen.count(it->first)) {
      ++it;
      continue;
    }
    if (profile_.protocol == PixelProtocol::KITTY && it->second.id) {
      out += "\x1b_Ga=d,d=I,i=" + std::to_string(it->second.id) + ",q=2\x1b\\";
    }
    it = shown_.erase(it);
  }
  return out;
}

std::string ImageEncoder::clear() {
  std::string out;
  if (profile_.protocol == PixelProtocol::KITTY) {
    for (auto & [key, shown] : shown_) {
      if (shown.id) out += "\x1b_Ga=d,d=I,i=" + std::to_string(shown.id) + ",q=2\x1b\\";
    }
  }
  shown_.clear();
  return out;
}

}  // namespace cellui::term
