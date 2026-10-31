#include <cellui/term/ImageEncoder.h>

#include <cellui/term/FrameEncoder.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// The part of a stretched picture that is shown: without its pad.
Image withoutPad(const Image & src) {
  if (src.pad <= 0) return src;
  Image out = src;
  out.pad = 0;
  out.width = std::max(1, src.width - 2 * src.pad);
  out.height = std::max(1, src.height - 2 * src.pad);
  out.pixels.assign(static_cast<size_t>(out.width) * static_cast<size_t>(out.height), 0);
  for (int y = 0; y < out.height; y++) {
    for (int x = 0; x < out.width; x++) {
      int sx = std::min(x + src.pad, src.width - 1), sy = std::min(y + src.pad, src.height - 1);
      out.pixels[static_cast<size_t>(y) * static_cast<size_t>(out.width) + static_cast<size_t>(x)] =
        src.pixels[static_cast<size_t>(sy) * static_cast<size_t>(src.width) + static_cast<size_t>(sx)];
    }
  }
  return out;
}

// The picture resampled to width x height (bilinear).
Image stretch(const Image & src, int width, int height) {
  Image out;
  out.width = std::max(1, width);
  out.height = std::max(1, height);
  out.version = src.version;
  out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
  auto at = [&](int x, int y) {
    x = std::clamp(x, 0, src.width - 1);
    y = std::clamp(y, 0, src.height - 1);
    return src.pixels[static_cast<size_t>(y) * static_cast<size_t>(src.width) + static_cast<size_t>(x)];
  };
  for (int y = 0; y < out.height; y++) {
    float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(src.height) / static_cast<float>(out.height) - 0.5f;
    int y0 = static_cast<int>(std::floor(fy));
    float ty = fy - static_cast<float>(y0);
    for (int x = 0; x < out.width; x++) {
      float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(src.width) / static_cast<float>(out.width) - 0.5f;
      int x0 = static_cast<int>(std::floor(fx));
      float tx = fx - static_cast<float>(x0);
      uint32_t a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
      auto mix = [&](uint8_t (*channel)(uint32_t)) {
        float top = static_cast<float>(channel(a)) * (1 - tx) + static_cast<float>(channel(b)) * tx;
        float bottom = static_cast<float>(channel(c)) * (1 - tx) + static_cast<float>(channel(d)) * tx;
        return static_cast<uint8_t>(top * (1 - ty) + bottom * ty + 0.5f);
      };
      out.pixels[static_cast<size_t>(y) * static_cast<size_t>(out.width) + static_cast<size_t>(x)] =
        Image::pack(mix(Image::red), mix(Image::green), mix(Image::blue), mix(Image::alpha));
    }
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

// The 8x8 Bayer matrix as thresholds in (0, 1).
float bayer8(int x, int y) {
  static const float * table = [] {
    static float t[64];
    int m[64] = {0};
    for (int size = 1; size < 8; size *= 2) {
      int next[64] = {0};
      for (int yy = 0; yy < size; yy++) {
        for (int xx = 0; xx < size; xx++) {
          int v = 4 * m[yy * 8 + xx];
          next[yy * 8 + xx] = v;
          next[yy * 8 + xx + size] = v + 2;
          next[(yy + size) * 8 + xx] = v + 3;
          next[(yy + size) * 8 + xx + size] = v + 1;
        }
      }
      std::copy(std::begin(next), std::end(next), std::begin(m));
    }
    for (int i = 0; i < 64; i++) t[i] = (static_cast<float>(m[i]) + 0.5f) / 64.0f;
    return t;
  }();
  return table[(y & 7) * 8 + (x & 7)];
}

// Of the two entries around `v` on a ramp of `levels`, the upper one when
// v lies above `threshold` of the way between them.
int orderedLevel(int v, const int * levels, int count, float threshold) {
  int i = 0;
  while (i + 2 < count && v >= levels[i + 1]) i++;
  float span = static_cast<float>(levels[i + 1] - levels[i]);
  float frac = std::clamp(static_cast<float>(v - levels[i]) / span, 0.0f, 1.0f);
  return frac > threshold ? i + 1 : i;
}

// A pixel's palette index by ordered dithering: each channel between its
// two cube levels, a grey on the grey ramp (with the cube's black and
// white at its ends) - exact for any gap, with no search.
int orderedIndex(int r, int g, int b, float threshold) {
  int hi = std::max({r, g, b}), lo = std::min({r, g, b});
  if (hi - lo < 12) {
    static const int kGreys[26] = {0, 8, 18, 28, 38, 48, 58, 68, 78, 88, 98, 108, 118, 128, 138, 148, 158, 168, 178, 188, 198, 208, 218, 228, 238, 255};
    int k = orderedLevel((r + g + b) / 3, kGreys, 26, threshold);
    return k == 0 ? 16 : k == 25 ? 231 : 232 + (k - 1);
  }
  return 16 + 36 * orderedLevel(r, kCubeLevels, 6, threshold) + 6 * orderedLevel(g, kCubeLevels, 6, threshold) +
         orderedLevel(b, kCubeLevels, 6, threshold);
}

int percent(int v) { return static_cast<int>(std::lround(v * 100.0 / 255.0)); }

uint32_t crc32(const uint8_t * data, size_t size, uint32_t crc = 0) {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t n = 0; n < 256; n++) {
      uint32_t c = n;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
      table[n] = c;
    }
    ready = true;
  }
  crc = ~crc;
  for (size_t i = 0; i < size; i++) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
  return ~crc;
}

void put32(std::vector<uint8_t> & out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

void chunk(std::vector<uint8_t> & out, const char * type, const std::vector<uint8_t> & data) {
  put32(out, static_cast<uint32_t>(data.size()));
  size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put32(out, crc32(out.data() + start, out.size() - start));
}

}  // namespace

ImageEncoder::Profile ImageEncoder::Profile::from(const Capabilities & caps) {
  Profile p;
  p.protocol = caps.pixels;
  p.cell_px_height = caps.display.cell_px_height;
  p.cell_px_width = caps.display.cell_px_width;
  p.sixel_setup = caps.sixel_setup;
  return p;
}

std::vector<uint8_t> encodePng(const Image & image, int x0, int y0, int width, int height) {
  // The scanlines, each led by filter type 0 (none).
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 4 + 1));
  for (int y = 0; y < height; y++) {
    raw.push_back(0);
    for (int x = 0; x < width; x++) {
      uint32_t p = image.pixels[static_cast<size_t>(y0 + y) * static_cast<size_t>(image.width) + static_cast<size_t>(x0 + x)];
      raw.push_back(Image::red(p));
      raw.push_back(Image::green(p));
      raw.push_back(Image::blue(p));
      raw.push_back(Image::alpha(p));
    }
  }
  // A zlib stream of stored deflate blocks.
  std::vector<uint8_t> z = {0x78, 0x01};
  size_t pos = 0;
  do {
    size_t n = std::min<size_t>(65535, raw.size() - pos);
    bool last = pos + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(static_cast<uint8_t>(n));
    z.push_back(static_cast<uint8_t>(n >> 8));
    z.push_back(static_cast<uint8_t>(~n));
    z.push_back(static_cast<uint8_t>(~n >> 8));
    z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
    pos += n;
  } while (pos < raw.size());
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  put32(z, (b << 16) | a);

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  std::vector<uint8_t> ihdr;
  put32(ihdr, static_cast<uint32_t>(width));
  put32(ihdr, static_cast<uint32_t>(height));
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA, no interlace
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});
  return png;
}

std::string encodeSixel(const Image & image, int x0, int y0, int width, int height, const std::vector<bool> & keep,
                        const Dither & dither) {
  // Each pixel's palette index, dithered.
  std::vector<int> indices(static_cast<size_t>(width) * static_cast<size_t>(height));
  auto pixel = [&](int x, int y) {
    return image.pixels[static_cast<size_t>(y0 + y) * static_cast<size_t>(image.width) + static_cast<size_t>(x0 + x)];
  };
  if (dither.method == Dither::Method::FLOYD_STEINBERG) {
    // Error rows with a border on both sides.
    std::vector<float> current(static_cast<size_t>(width + 2) * 3, 0.0f), next(current.size(), 0.0f);
    for (int y = 0; y < height; y++) {
      std::fill(next.begin(), next.end(), 0.0f);
      for (int x = 0; x < width; x++) {
        uint32_t p = pixel(x, y);
        size_t e = static_cast<size_t>(x + 1) * 3;
        float want[3] = {Image::red(p) + current[e], Image::green(p) + current[e + 1], Image::blue(p) + current[e + 2]};
        int c[3];
        for (size_t k = 0; k < 3; k++) c[k] = static_cast<int>(std::lround(std::clamp(want[k], 0.0f, 255.0f)));
        int index = nearest256(Color(c[0], c[1], c[2]));
        indices[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = index;
        int got[3];
        paletteRgb(index, got[0], got[1], got[2]);
        for (size_t k = 0; k < 3; k++) {
          float error = want[k] - static_cast<float>(got[k]);
          current[e + 3 + k] += error * 7.0f / 16.0f;
          next[e - 3 + k] += error * 3.0f / 16.0f;
          next[e + k] += error * 5.0f / 16.0f;
          next[e + 3 + k] += error * 1.0f / 16.0f;
        }
      }
      current.swap(next);
    }
  } else {
    std::unordered_map<uint32_t, int> cache;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        uint32_t p = pixel(x, y);
        int index;
        if (dither.method == Dither::Method::ORDERED) {
          index = orderedIndex(Image::red(p), Image::green(p), Image::blue(p), bayer8(dither.origin_x + x, dither.origin_y + y));
        } else {
          auto it = cache.find(p & 0x00ffffffu);
          index = it != cache.end() ? it->second : nearest256(Color(Image::red(p), Image::green(p), Image::blue(p)));
          cache.emplace(p & 0x00ffffffu, index);
        }
        indices[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = index;
      }
    }
  }

  // Each pixel's register (its palette index - 16), or -1 when left out.
  std::vector<int> regs(static_cast<size_t>(width) * static_cast<size_t>(height), -1);
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
      regs[i] = indices[i] - 16;
      used.insert(regs[i]);
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
  int rows = p.image->coveredRows(ch), cols = p.image->coveredCols(cw);
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
  size_t upload_before = sizes_.upload;
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
      size_t chunk_start = out.size();
      out += "\x1b_G";
      if (i == 0) out += "a=t,f=24,s=" + std::to_string(img.width) + ",v=" + std::to_string(img.height) + ",i=" + std::to_string(shown.id) + ",";
      out += "q=1,m=" + std::string(more ? "1" : "0") + ";";
      out += data.substr(i, kKittyChunk);
      out += "\x1b\\";
      sizes_.upload += out.size() - chunk_start;
      if (data.empty()) break;
    }
    picture_stats_[{img.width, img.height}].uploads++;
    picture_stats_[{img.width, img.height}].bytes += sizes_.upload - upload_before;
  }
  std::vector<Rect> rects = visibleRects(frame, p, false);
  if (changed || cleared || rects != shown.rects || p.row != shown.row || p.col != shown.col) {
    int ch = profile_.cell_px_height, cw = profile_.cell_px_width;
    for (size_t k = 0; k < rects.size(); k++) {
      const Rect & r = rects[k];
      int x0, x1, y0, y1;
      img.pixelSpan(r.col - p.col, r.cols, cw, true, x0, x1);
      img.pixelSpan(r.row - p.row, r.rows, ch, false, y0, y1);
      if (x1 <= x0 || y1 <= y0) continue;
      size_t place_start = out.size();
      out += cup(r.row, r.col);
      out += "\x1b_Ga=p,i=" + std::to_string(shown.id) + ",p=" + std::to_string(k + 1) + ",x=" + std::to_string(x0) +
             ",y=" + std::to_string(y0) + ",w=" + std::to_string(x1 - x0) + ",h=" + std::to_string(y1 - y0);
      // A stretched picture is scaled by the terminal over its cells.
      if (img.stretched()) out += ",c=" + std::to_string(r.cols) + ",r=" + std::to_string(r.rows);
      out += ",C=1,q=1\x1b\\";
      sizes_.placement += out.size() - place_start;
    }
    // Placements this picture no longer has.
    if (!changed && !cleared) {
      for (size_t k = rects.size(); k < shown.rects.size(); k++) {
        size_t before = out.size();
        out += "\x1b_Ga=d,d=i,i=" + std::to_string(shown.id) + ",p=" + std::to_string(k + 1) + ",q=1\x1b\\";
        sizes_.placement += out.size() - before;
      }
    }
    shown.rects = rects;
    shown.row = p.row;
    shown.col = p.col;
  }
  if (changed && old_id) {
    std::string del = "\x1b_Ga=d,d=I,i=" + std::to_string(old_id) + ",q=1\x1b\\";
    sizes_.placement += del.size();
    out += del;
  }
  return out;
}

std::string ImageEncoder::sixel(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared) {
  std::vector<Rect> rects = visibleRects(frame, p, true);
  bool changed = fresh || cleared || shown.version != p.image->version || rects != shown.rects || p.row != shown.row || p.col != shown.col;
  shown.version = p.image->version;
  shown.rects = rects;
  shown.row = p.row;
  shown.col = p.col;
  if (!changed || rects.empty()) return "";
  int ch = profile_.cell_px_height, cw = profile_.cell_px_width;
  // Sixel draws a pixel to a pixel, so a stretched picture is scaled here.
  Image natural;
  if (p.image->stretched()) natural = stretch(withoutPad(*p.image), p.image->cols * cw, p.image->rows * ch);
  const Image & img = p.image->stretched() ? natural : *p.image;
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
  sizes_.placement += out.size();
  Dither dither;
  dither.method = profile_.dither;
  dither.origin_x = p.col * cw + x;
  dither.origin_y = p.row * ch + y;
  size_t before = out.size();
  out += encodeSixel(img, x, y, w, h, keep, dither);
  sizes_.upload += out.size() - before;
  picture_stats_[{p.image->width, p.image->height}].uploads++;
  picture_stats_[{p.image->width, p.image->height}].bytes += out.size() - before;
  return out;
}

std::string ImageEncoder::iterm2(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared) {
  const Image & img = *p.image;
  std::vector<Rect> rects = visibleRects(frame, p, true);
  bool changed = fresh || cleared || shown.version != img.version || rects != shown.rects || p.row != shown.row || p.col != shown.col;
  shown.version = img.version;
  shown.rects = rects;
  shown.row = p.row;
  shown.col = p.col;
  if (!changed) return "";
  int ch = profile_.cell_px_height, cw = profile_.cell_px_width;
  std::string out;
  for (const Rect & r : rects) {
    int x0, x1, y0, y1;
    img.pixelSpan(r.col - p.col, r.cols, cw, true, x0, x1);
    img.pixelSpan(r.row - p.row, r.rows, ch, false, y0, y1);
    if (x1 <= x0 || y1 <= y0) continue;
    std::vector<uint8_t> png = encodePng(img, x0, y0, x1 - x0, y1 - y0);
    // Sized in cells, so the terminal's own pixel scale does not matter
    // (and a stretched picture is scaled by it).
    int rows = img.stretched() ? r.rows : (y1 - y0 + ch - 1) / ch, cols = img.stretched() ? r.cols : (x1 - x0 + cw - 1) / cw;
    size_t start = out.size();
    out += cup(r.row, r.col);
    sizes_.placement += out.size() - start;
    size_t osc_start = out.size();
    out += "\x1b]1337;File=inline=1;size=" + std::to_string(png.size()) + ";width=" + std::to_string(cols) +
           ";height=" + std::to_string(rows) + ";preserveAspectRatio=0;doNotMoveCursor=1:" + base64(png) + "\a";
    sizes_.upload += out.size() - osc_start;
    picture_stats_[{img.width, img.height}].uploads++;
    picture_stats_[{img.width, img.height}].bytes += out.size() - osc_start;
  }
  return out;
}

std::string ImageEncoder::encode(const CellBuffer & frame, const std::vector<ImagePlacement> & images, bool cleared) {
  std::string out;
  sizes_ = {};
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
    switch (profile_.protocol) {
      case PixelProtocol::KITTY: out += kitty(frame, p, shown, fresh, cleared); break;
      case PixelProtocol::ITERM2: out += iterm2(frame, p, shown, fresh, cleared); break;
      default: out += sixel(frame, p, shown, fresh, cleared); break;
    }
  }
  for (auto it = shown_.begin(); it != shown_.end();) {
    if (seen.count(it->first)) {
      ++it;
      continue;
    }
    if (profile_.protocol == PixelProtocol::KITTY && it->second.id) {
      std::string del = "\x1b_Ga=d,d=I,i=" + std::to_string(it->second.id) + ",q=1\x1b\\";
      sizes_.placement += del.size();
      out += del;
    }
    it = shown_.erase(it);
  }
  return out;
}

std::string ImageEncoder::handleKittyReply(const std::string & body) const {
  size_t semi = body.find(';');
  std::string message = semi == std::string::npos ? "" : body.substr(semi + 1);
  if (message == "OK") return "";
  std::string keys = body.substr(0, semi);
  uint32_t id = 0;
  size_t at = keys.find("i=");
  if (at != std::string::npos) id = static_cast<uint32_t>(std::strtoul(keys.c_str() + at + 2, nullptr, 10));
  std::string who = "kitty graphics";
  for (const auto & [key, shown] : shown_) {
    if (id && shown.id == id) who += " image key " + std::to_string(key);
  }
  return who + " (id " + std::to_string(id) + "): " + (message.empty() ? keys : message);
}

std::string ImageEncoder::clear() {
  std::string out;
  if (profile_.protocol == PixelProtocol::KITTY) {
    for (auto & [key, shown] : shown_) {
      if (shown.id) out += "\x1b_Ga=d,d=I,i=" + std::to_string(shown.id) + ",q=1\x1b\\";
    }
  }
  shown_.clear();
  return out;
}

}  // namespace cellui::term
