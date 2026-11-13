#include <cellui/StageStats.h>
#include <cellui/term/ImageEncoder.h>

#include <cellui/term/Deflate.h>
#include <cellui/term/FrameEncoder.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace cellui::term {

namespace {

constexpr int kCubeLevels[6] = {0, 95, 135, 175, 215, 255};
// The kitty protocol's largest chunk of base64 data.
constexpr size_t kKittyChunk = 4096;

std::string base64(const std::vector<uint8_t> & data) {
  StageTimer timer(Stage::BASE64);
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

// Where a pixel is in the Bayer matrix, 0 to 63.
int bayerIndex(int x, int y) { return (y & 7) * 8 + (x & 7); }

// Offset, in levels peak to peak, an ordered dither adds before a pixel goes to its nearest register.
constexpr float kOrderedAmplitude = 12.0f;
// A pixel off its nearest register by no more than this in any channel is not dithered.
constexpr int kDitherAbove = 5;
// On the Sixel grid, a pixel differing from its grid colour by no more than this (whole 8-bit levels) is not dithered.
constexpr int kGridTolerance = 0;
// A picture of this many pixels or fewer is sent in whichever is smaller of its palette PNG and its pixels.
constexpr size_t kSmallPicturePixels = 16384;
// A changed Sixel picture is considered for sending in pieces only when the pieces cover less than this
// share of its cells, and is then sent in whichever way is smaller: a piece repeats the registers it
// uses, and most changes (a line moving across a chart) touch every row.
constexpr size_t kPartialBelowPercent = 80;

int percentOf(int v) { return static_cast<int>(std::lround(v * 100.0 / 255.0)); }

// The same as percentOf() and onSixelGrid() for a level, from a table made once: a picture asks for both
// of every pixel.
struct LevelTables {
  uint8_t percent[256];
  uint8_t grid[256];
  LevelTables() {
    for (int v = 0; v < 256; v++) {
      percent[v] = static_cast<uint8_t>(percentOf(v));
      grid[v] = static_cast<uint8_t>(std::lround(percentOf(v) * 255.0 / 100.0));
    }
  }
};
const LevelTables & levelTables() {
  static const LevelTables tables;
  return tables;
}

int percent(int v) { return v >= 0 && v < 256 ? levelTables().percent[v] : percentOf(v); }

// What the dither does to a level at each place of the matrix, made once from the same expressions the
// pixels used to be worked out with: the level moved by the matrix's offset and put on the 101 levels
// (`grid`, for a gradient between the terminal's levels), or only moved (`ordered`, between a palette's
// registers). A picture asks for both of every pixel.
struct DitherTables {
  uint8_t grid[64][256];
  uint8_t ordered[64][256];
  DitherTables() {
    const LevelTables & levels = levelTables();
    for (int i = 0; i < 64; i++) {
      float threshold = bayer8(i & 7, i >> 3);
      float grid_offset = (threshold - 0.5f) * (255.0f / 100.0f);
      float ordered_offset = (threshold - 0.5f) * kOrderedAmplitude;
      for (int v = 0; v < 256; v++) {
        grid[i][v] = levels.grid[std::clamp(static_cast<int>(std::lround(static_cast<float>(v) + grid_offset)), 0, 255)];
        ordered[i][v] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(static_cast<float>(v) + ordered_offset)), 0, 255));
      }
    }
  }
};
const DitherTables & ditherTables() {
  static const DitherTables tables;
  return tables;
}

// The terminal's own default 256-colour palette beyond the 16 base colours (the 6x6x6 cube and 24 greys:
// entries 16 to 255), as a fixed palette: register n is entry n + 16. A picture drawn in it carries no
// palette of its own to compute, and a smooth dark gradient maps to long runs of a few greys.
constexpr int kXtermCube[6] = {0, 95, 135, 175, 215, 255};
constexpr int kXtermRegisters = 240;
// The most registers a palette of ours has (the terminal's 128 at most, the default palette's 240).
constexpr size_t kMaxSixelRegisters = 256;

uint32_t xtermColour(int reg) {
  int index = reg + 16, r, g, b;
  if (index >= 232) {
    r = g = b = 8 + 10 * (index - 232);
  } else {
    int i = index - 16;
    r = kXtermCube[i / 36];
    g = kXtermCube[(i / 6) % 6];
    b = kXtermCube[i % 6];
  }
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

// Of the two entries around `v` on a ramp, the upper one when v lies above `threshold` of the way between.
int xtermLevel(int v, const int * levels, int count, float threshold) {
  int i = 0;
  while (i + 2 < count && v >= levels[i + 1]) i++;
  float span = static_cast<float>(levels[i + 1] - levels[i]);
  float frac = std::clamp(static_cast<float>(v - levels[i]) / span, 0.0f, 1.0f);
  return frac > threshold ? i + 1 : i;
}

// A pixel's register in the fixed palette by ordered dithering: each channel between its two cube levels,
// a grey on the grey ramp (the cube's black and white at its ends).
int xtermRegister(int r, int g, int b, float threshold) {
  int hi = std::max({r, g, b}), lo = std::min({r, g, b});
  if (hi - lo < 12) {
    static const int kGreys[26] = {0, 8, 18, 28, 38, 48, 58, 68, 78, 88, 98, 108, 118, 128, 138, 148, 158, 168, 178, 188, 198, 208, 218, 228, 238, 255};
    int k = xtermLevel((r + g + b) / 3, kGreys, 26, threshold);
    return (k == 0 ? 16 : k == 25 ? 231 : 232 + (k - 1)) - 16;
  }
  return 36 * xtermLevel(r, kXtermCube, 6, threshold) + 6 * xtermLevel(g, kXtermCube, 6, threshold) + xtermLevel(b, kXtermCube, 6, threshold);
}

// The 8-bit level a Sixel colour definition (percent) makes of `v`: the level the terminal shows.
int onSixelGrid(int v) { return v >= 0 && v < 256 ? levelTables().grid[v] : static_cast<int>(std::lround(percentOf(v) * 255.0 / 100.0)); }

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
  if (caps.sixel_registers > 0) p.sixel_registers = std::min(caps.sixel_registers, 128);
  p.sixel_registers_total = caps.sixel_registers;
  return p;
}

std::vector<uint8_t> encodePng(const Image & image, int x0, int y0, int width, int height) {
  StageTimer timer(Stage::PNG);
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
  std::vector<uint8_t> z = zlibCompress(raw);

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

namespace {

// A count of 24-bit colours in an open-addressing table, for a picture with thousands of colours a pixel
// asks about: the hash maps this replaces were most of the palette's time.
class ColourCounts {
 public:
  explicit ColourCounts(size_t expected) {
    size_t cap = 256;
    while (cap < expected * 2) cap *= 2;
    keys_.assign(cap, kEmpty);
    counts_.assign(cap, 0);
  }
  void add(uint32_t colour, uint32_t n = 1) {
    if ((used_ + 1) * 2 > keys_.size()) grow();
    size_t i = slot(colour);
    while (keys_[i] != kEmpty && keys_[i] != colour) i = (i + 1) & (keys_.size() - 1);
    if (keys_[i] == kEmpty) {
      keys_[i] = colour;
      used_++;
    }
    counts_[i] += n;
  }
  size_t size() const { return used_; }
  // The (colour, count) pairs in colour order.
  std::vector<std::pair<uint32_t, uint32_t>> sorted() const {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    out.reserve(used_);
    for (size_t i = 0; i < keys_.size(); i++) {
      if (keys_[i] != kEmpty) out.emplace_back(keys_[i], counts_[i]);
    }
    // By colour, as sorting the pairs would (no two have the same colour): three passes of a byte each.
    std::vector<std::pair<uint32_t, uint32_t>> tmp(out.size());
    for (int shift = 0; shift < 24; shift += 8) {
      size_t start[257] = {0};
      for (const auto & c : out) start[((c.first >> shift) & 0xff) + 1]++;
      for (size_t k = 0; k < 256; k++) start[k + 1] += start[k];
      for (const auto & c : out) tmp[start[(c.first >> shift) & 0xff]++] = c;
      out.swap(tmp);
    }
    return out;
  }

 private:
  static constexpr uint32_t kEmpty = 0xffffffffu;  // a colour is 24 bits
  size_t slot(uint32_t colour) const { return (colour * 2654435761u >> 8) & (keys_.size() - 1); }
  void grow() {
    std::vector<uint32_t> keys = std::move(keys_);
    std::vector<uint32_t> counts = std::move(counts_);
    keys_.assign(keys.size() * 2, kEmpty);
    counts_.assign(keys.size() * 2, 0);
    for (size_t j = 0; j < keys.size(); j++) {
      if (keys[j] == kEmpty) continue;
      size_t i = slot(keys[j]);
      while (keys_[i] != kEmpty) i = (i + 1) & (keys_.size() - 1);
      keys_[i] = keys[j];
      counts_[i] = counts[j];
    }
  }
  std::vector<uint32_t> keys_, counts_;
  size_t used_ = 0;
};

// The colours of a Sixel image: a register each, either exactly the colours the pixels have (when
// they fit) or their median cut. Made once for a picture, so every piece of it sent later is drawn in
// the same colours.
struct SixelPalette {
  std::vector<uint32_t> rgb;  // 0xRRGGBB per register
  bool exact = false;         // every pixel's colour is a register: nothing to dither
  bool grid = false;          // colours are first put on the 101 levels a Sixel colour definition has
  bool grid_dither = false;   // and a gradient (more colours than registers) is dithered between those levels
  bool fixed = false;         // the terminal's default palette (kXtermRegisters registers), not made from the picture
  std::unordered_map<uint32_t, int> index;  // (exact) colour -> register
  // The registers' levels a channel at a time, for a search that goes over them all (see finish()).
  std::vector<int> red, green, blue;

  // Called when the registers are all there: lays them out for paletteNearest().
  void finish() {
    red.resize(rgb.size());
    green.resize(rgb.size());
    blue.resize(rgb.size());
    for (size_t i = 0; i < rgb.size(); i++) {
      red[i] = static_cast<int>((rgb[i] >> 16) & 0xff);
      green[i] = static_cast<int>((rgb[i] >> 8) & 0xff);
      blue[i] = static_cast<int>(rgb[i] & 0xff);
    }
  }
};

int paletteNearest(const SixelPalette & palette, int r, int g, int b) {
  // The distance to every register (green counts most, blue least) in one loop a compiler can vectorise,
  // then the first of the nearest, as the one-at-a-time search it replaces took it.
  const size_t n = palette.red.size();
  int distance[kMaxSixelRegisters];
  if (n == 0 || n > kMaxSixelRegisters) {
    int best = 0;
    long best_d = -1;
    for (size_t i = 0; i < palette.rgb.size(); i++) {
      uint32_t c = palette.rgb[i];
      long dr = static_cast<long>((c >> 16) & 0xff) - r, dg = static_cast<long>((c >> 8) & 0xff) - g, db = static_cast<long>(c & 0xff) - b;
      long d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
      if (best_d < 0 || d < best_d) {
        best_d = d;
        best = static_cast<int>(i);
      }
    }
    return best;
  }
  const int * pr = palette.red.data();
  const int * pg = palette.green.data();
  const int * pb = palette.blue.data();
  for (size_t i = 0; i < n; i++) {
    int dr = pr[i] - r, dg = pg[i] - g, db = pb[i] - b;
    distance[i] = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
  }
  size_t best = 0;
  for (size_t i = 1; i < n; i++) {
    if (distance[i] < distance[best]) best = i;
  }
  return static_cast<int>(best);
}

// The colour a pixel is shown in on the Sixel grid: its levels snapped to the grid's, after the ordered
// dither's offset of one grid step if `dither` is given, so a smooth gradient is not banded by the
// terminal's 101 levels. x and y are the pixel's place in the piece the dither's origin belongs to.
void gridColour(uint32_t p, const Dither * dither, int x, int y, int & r, int & g, int & b) {
  // A colour within half a level of the grid's is not dithered: its error cannot be seen, and a flat area
  // of such a colour stays one register.
  int pr = onSixelGrid(Image::red(p)), pg = onSixelGrid(Image::green(p)), pb = onSixelGrid(Image::blue(p));
  if (std::max({std::abs(pr - Image::red(p)), std::abs(pg - Image::green(p)), std::abs(pb - Image::blue(p))}) <= kGridTolerance) {
    r = pr, g = pg, b = pb;
    return;
  }
  if (!dither) {
    r = pr, g = pg, b = pb;
    return;
  }
  const uint8_t * snap = ditherTables().grid[bayerIndex(dither->origin_x + x, dither->origin_y + y)];
  r = snap[Image::red(p)], g = snap[Image::green(p)], b = snap[Image::blue(p)];
}

// The pixels' colours cut into at most `registers` boxes, repeatedly splitting the box with the most
// pixels times its widest range at the weighted median of that channel; a box's register is its mean.
SixelPalette makeSixelPalette(const Image & image, int x0, int y0, int width, int height, const std::vector<bool> & keep, int registers,
                              bool grid = false, const Dither * dither = nullptr) {
  StageTimer timer(Stage::SIXEL_PALETTE);
  // A picture with more colours than registers is a gradient (a blurred backdrop): it is dithered, where a
  // flat one (text, a chart) stays as it is.
  bool gradient = false;
  const bool mix = dither && dither->mix;
  if (mix) {
    // the colours are only put on the terminal's levels (below), never dithered first
  } else if (grid && dither && dither->method != Dither::Method::NEAREST && dither->gradient) {
    gradient = true;
  } else if (grid && dither && dither->method != Dither::Method::NEAREST) {
    ColourCounts distinct(static_cast<size_t>(std::max(2, registers)) * 2);
    for (int y = 0; y < height && !gradient; y++) {
      for (int x = 0; x < width && !gradient; x++) {
        uint32_t p = image.pixels[static_cast<size_t>(y0 + y) * static_cast<size_t>(image.width) + static_cast<size_t>(x0 + x)];
        if (!keep[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] || Image::alpha(p) < 192) continue;
        distinct.add(p & 0xffffff);
        gradient = distinct.size() > static_cast<size_t>(std::max(2, registers));
      }
    }
  }
  ColourCounts counts(std::min<size_t>(static_cast<size_t>(width) * static_cast<size_t>(height) / 4, 16384));
  for (int y = 0; y < height; y++) {
    uint32_t run_colour = 0, run = 0;  // a run of the same colour is counted once
    for (int x = 0; x < width; x++) {
      uint32_t p = image.pixels[static_cast<size_t>(y0 + y) * static_cast<size_t>(image.width) + static_cast<size_t>(x0 + x)];
      if (!keep[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] || Image::alpha(p) < 192) continue;
      int r = Image::red(p), g = Image::green(p), b = Image::blue(p);
      // Two colours the terminal cannot tell apart are one: they must not count against the registers,
      // and the median cut must not average a flat colour with its neighbours because of them.
      if (gradient) gridColour(p, dither, x, y, r, g, b);
      else if (grid) r = onSixelGrid(r), g = onSixelGrid(g), b = onSixelGrid(b);
      uint32_t colour = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
      if (run && colour == run_colour) {
        run++;
        continue;
      }
      if (run) counts.add(run_colour, run);
      run_colour = colour;
      run = 1;
    }
    if (run) counts.add(run_colour, run);
  }
  SixelPalette palette;
  palette.grid = grid;
  palette.grid_dither = gradient;
  std::vector<std::pair<uint32_t, uint32_t>> colours = counts.sorted();  // in colour order, whatever the table did
  registers = std::max(2, registers);
  if (colours.size() <= static_cast<size_t>(registers)) {
    palette.exact = true;
    for (const auto & [rgb, n] : colours) {
      palette.index[rgb] = static_cast<int>(palette.rgb.size());
      palette.rgb.push_back(rgb);
    }
    palette.finish();
    return palette;
  }
  struct Box {
    size_t begin, end;
    uint64_t pixels = 0;
    int channel = 0, range = 0;
  };
  auto measure = [&](Box & box) {
    int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    box.pixels = 0;
    for (size_t i = box.begin; i < box.end; i++) {
      uint32_t c = colours[i].first;
      int v[3] = {static_cast<int>((c >> 16) & 0xff), static_cast<int>((c >> 8) & 0xff), static_cast<int>(c & 0xff)};
      for (int k = 0; k < 3; k++) {
        lo[k] = std::min(lo[k], v[k]);
        hi[k] = std::max(hi[k], v[k]);
      }
      box.pixels += colours[i].second;
    }
    box.channel = 1;  // green counts most, so it wins a tie
    box.range = hi[1] - lo[1];
    for (int k : {0, 2}) {
      if (hi[k] - lo[k] > box.range) {
        box.range = hi[k] - lo[k];
        box.channel = k;
      }
    }
  };
  std::vector<Box> boxes = {Box{0, colours.size()}};
  measure(boxes[0]);
  while (boxes.size() < static_cast<size_t>(registers)) {
    size_t pick = boxes.size();
    double best = 0;
    for (size_t i = 0; i < boxes.size(); i++) {
      if (boxes[i].end - boxes[i].begin < 2) continue;
      double score = static_cast<double>(boxes[i].pixels) * boxes[i].range;
      if (score > best) {
        best = score;
        pick = i;
      }
    }
    if (pick == boxes.size()) break;
    Box box = boxes[pick];
    int shift = box.channel == 0 ? 16 : box.channel == 1 ? 8 : 0;
    std::sort(colours.begin() + static_cast<std::ptrdiff_t>(box.begin), colours.begin() + static_cast<std::ptrdiff_t>(box.end),
              [&](const auto & a, const auto & b) { return ((a.first >> shift) & 0xff) < ((b.first >> shift) & 0xff); });
    uint64_t half = box.pixels / 2, seen = 0;
    size_t cut = box.begin;
    while (cut + 1 < box.end && seen + colours[cut].second <= half) seen += colours[cut++].second;
    cut = std::clamp(cut, box.begin + 1, box.end - 1);
    Box low{box.begin, cut}, high{cut, box.end};
    measure(low);
    measure(high);
    boxes[pick] = low;
    boxes.push_back(high);
  }
  for (const Box & box : boxes) {
    double sum[3] = {0, 0, 0};
    for (size_t i = box.begin; i < box.end; i++) {
      uint32_t c = colours[i].first;
      double n = colours[i].second;
      sum[0] += n * static_cast<double>((c >> 16) & 0xff);
      sum[1] += n * static_cast<double>((c >> 8) & 0xff);
      sum[2] += n * static_cast<double>(c & 0xff);
    }
    double n = static_cast<double>(std::max<uint64_t>(box.pixels, 1));
    palette.rgb.push_back((static_cast<uint32_t>(std::lround(sum[0] / n)) << 16) | (static_cast<uint32_t>(std::lround(sum[1] / n)) << 8) |
                          static_cast<uint32_t>(std::lround(sum[2] / n)));
  }
  palette.finish();
  return palette;
}

SixelPalette xtermPalette() {
  SixelPalette palette;
  palette.fixed = true;
  for (int reg = 0; reg < kXtermRegisters; reg++) palette.rgb.push_back(xtermColour(reg));
  palette.finish();
  return palette;
}

// Ordered dither for any palette: the colour's candidates are the registers that, one after another, come
// nearest it with the error of the ones before carried on; sorted by brightness, the Bayer threshold picks
// one, so the registers' average over a patch is the colour and no two neighbours need agree.
constexpr int kMixCandidates = 16;
struct MixPick {
  uint32_t key = 0xffffffffu;
  uint8_t reg[kMixCandidates] = {};
};

MixPick mixPick(const SixelPalette & palette, int r, int g, int b, uint32_t key) {
  MixPick pick;
  pick.key = key;
  float err[3] = {0, 0, 0};
  const float want[3] = {static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)};
  int chosen[kMixCandidates];
  for (int i = 0; i < kMixCandidates; i++) {
    float t[3];
    int c[3];
    for (int k = 0; k < 3; k++) {
      t[k] = std::clamp(want[k] + err[k], 0.0f, 255.0f);
      c[k] = static_cast<int>(std::lround(t[k]));
    }
    int reg = paletteNearest(palette, c[0], c[1], c[2]);
    chosen[i] = reg;
    const size_t u = static_cast<size_t>(reg);
    err[0] += want[0] - static_cast<float>(palette.red[u]);
    err[1] += want[1] - static_cast<float>(palette.green[u]);
    err[2] += want[2] - static_cast<float>(palette.blue[u]);
  }
  auto luma = [&](int reg) {
    const size_t u = static_cast<size_t>(reg);
    return 299 * palette.red[u] + 587 * palette.green[u] + 114 * palette.blue[u];
  };
  std::stable_sort(chosen, chosen + kMixCandidates, [&](int x, int y) { return luma(x) < luma(y); });
  for (int i = 0; i < kMixCandidates; i++) pick.reg[i] = static_cast<uint8_t>(chosen[i]);
  return pick;
}

// The pixels as Sixel in the colours of `palette`: covered pixels (`keep` false, one entry per pixel)
// are left transparent. A palette that holds the picture's colours exactly draws them as they are;
// otherwise each pixel goes to the nearest register, after the dither's offset or with its error
// carried on.
// Each pixel's register in `palette` (or -1 where it is left out: covered, or too transparent to
// draw); `transparent` says whether any pixel was, or the height is not a whole number of Sixel bands.
std::vector<int> mapToRegisters(const SixelPalette & palette, const Image & image, int x0, int y0, int width, int height,
                                const std::vector<bool> & keep, const Dither & dither, bool & transparent) {
  StageTimer timer(Stage::SIXEL_MAP);
  auto pixel = [&](int x, int y) {
    return image.pixels[static_cast<size_t>(y0 + y) * static_cast<size_t>(image.width) + static_cast<size_t>(x0 + x)];
  };
  // Each pixel's register, or -1 when left out.
  std::vector<int> regs(static_cast<size_t>(width) * static_cast<size_t>(height), -1);
  transparent = transparent || height % 6 != 0;
  auto left_out = [&](size_t i, uint32_t p) { return !keep[i] || Image::alpha(p) < 192; };
  if (palette.fixed) {
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        size_t i = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
        uint32_t p = pixel(x, y);
        if (left_out(i, p)) {
          transparent = true;
          continue;
        }
        float threshold = dither.method == Dither::Method::ORDERED ? bayer8(dither.origin_x + x, dither.origin_y + y) : 0.5f;
        regs[i] = xtermRegister(Image::red(p), Image::green(p), Image::blue(p), threshold);
      }
    }
    return regs;
  }
  // (offset) colour -> nearest register, in a table the colours map into by their hash: one that is
  // pushed out by another is only worked out again, so what comes back is the same.
  struct Cached {
    uint32_t key = 0xffffffffu;
    int reg = 0;
  };
  size_t cache_bits = 8;
  while (cache_bits < 14 && (size_t{1} << cache_bits) < static_cast<size_t>(width) * static_cast<size_t>(height) / 2) cache_bits++;
  std::vector<Cached> cache(size_t{1} << cache_bits);
  const size_t mix_cache_bits = cache_bits;
  std::vector<MixPick> mix_cache(size_t{1} << mix_cache_bits);
  auto nearest = [&](int r, int g, int b) {
    uint32_t key = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    Cached & slot = cache[(key * 2654435761u) >> (32 - cache_bits)];
    if (slot.key == key) return slot.reg;
    slot.key = key;
    slot.reg = paletteNearest(palette, r, g, b);
    return slot.reg;
  };
  std::vector<float> current(static_cast<size_t>(width + 2) * 3, 0.0f), next(current.size(), 0.0f);
  bool diffuse = !palette.exact && dither.method == Dither::Method::FLOYD_STEINBERG;
  for (int y = 0; y < height; y++) {
    std::fill(next.begin(), next.end(), 0.0f);
    for (int x = 0; x < width; x++) {
      size_t i = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
      uint32_t p = pixel(x, y);
      if (left_out(i, p)) {
        transparent = true;
        continue;
      }
      int r = Image::red(p), g = Image::green(p), b = Image::blue(p);
      if (palette.grid_dither) gridColour(p, &dither, x, y, r, g, b);
      else if (palette.grid) r = onSixelGrid(r), g = onSixelGrid(g), b = onSixelGrid(b);
      if (palette.exact) {
        regs[i] = palette.index.at(((static_cast<uint32_t>(r)) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b));
        continue;
      }
      if (diffuse) {
        size_t e = static_cast<size_t>(x + 1) * 3;
        float want[3] = {static_cast<float>(r) + current[e], static_cast<float>(g) + current[e + 1], static_cast<float>(b) + current[e + 2]};
        int c[3];
        for (size_t k = 0; k < 3; k++) c[k] = static_cast<int>(std::lround(std::clamp(want[k], 0.0f, 255.0f)));
        int reg = nearest(c[0], c[1], c[2]);
        regs[i] = reg;
        uint32_t got = palette.rgb[static_cast<size_t>(reg)];
        float have[3] = {static_cast<float>((got >> 16) & 0xff), static_cast<float>((got >> 8) & 0xff), static_cast<float>(got & 0xff)};
        for (size_t k = 0; k < 3; k++) {
          float error = want[k] - have[k];
          current[e + 3 + k] += error * 7.0f / 16.0f;
          next[e - 3 + k] += error * 3.0f / 16.0f;
          next[e + k] += error * 5.0f / 16.0f;
          next[e + 3 + k] += error * 1.0f / 16.0f;
        }
        continue;
      }
      if (dither.mix) {
        // Between the nearest entry and the one that, mixed with it, comes nearest the pixel: the Bayer
        // matrix says which, by how far along the pair the pixel lies.
        uint32_t key = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        MixPick & pick = mix_cache[(key * 2654435761u) >> (32 - mix_cache_bits)];
        if (pick.key != key) pick = mixPick(palette, r, g, b, key);
        float threshold = bayer8(dither.origin_x + x, dither.origin_y + y);
        regs[i] = pick.reg[std::min(kMixCandidates - 1, static_cast<int>(threshold * kMixCandidates))];
        continue;
      }
      int reg = nearest(r, g, b);
      if (dither.method == Dither::Method::ORDERED) {
        // A pixel the nearest register already matches closely stays: a flat area must not turn into a
        // pattern of near-equal registers. Where the match is poor (a gradient between registers a few
        // levels apart) an offset fixed to the screen turns the band edge into a fine pattern.
        uint32_t c = palette.rgb[static_cast<size_t>(reg)];
        int miss = std::max({std::abs(static_cast<int>((c >> 16) & 0xff) - r), std::abs(static_cast<int>((c >> 8) & 0xff) - g),
                             std::abs(static_cast<int>(c & 0xff) - b)});
        if (miss > kDitherAbove) {
          const uint8_t * moved = ditherTables().ordered[bayerIndex(dither.origin_x + x, dither.origin_y + y)];
          reg = nearest(moved[r], moved[g], moved[b]);
        }
      }
      regs[i] = reg;
    }
    if (diffuse) current.swap(next);
  }
  return regs;
}

std::string encodeSixelWith(const SixelPalette & palette, const Image & image, int x0, int y0, int width, int height,
                            const std::vector<bool> & keep, const Dither & dither) {
  StageTimer timer(Stage::SIXEL_ENCODE);
  bool transparent = false;
  std::vector<int> regs = mapToRegisters(palette, image, x0, y0, width, height, keep, dither, transparent);
  // The registers the picture uses, and which of them each band of six rows does: a band is gone over once,
  // setting a bit in the plane of the register at each pixel, where it was gone over once a register.
  const size_t palette_size = palette.rgb.size();
  std::vector<char> in_picture(palette_size, 0);
  for (int reg : regs) {
    if (reg >= 0) in_picture[static_cast<size_t>(reg)] = 1;
  }
  std::string out = "\x1bP0;" + std::string(transparent ? "1" : "0") + ";0q\"1;1;" + std::to_string(width) + ";" + std::to_string(height);
  for (size_t reg = 0; reg < palette_size; reg++) {
    if (!in_picture[reg]) continue;
    uint32_t c = palette.rgb[reg];
    out += "#" + std::to_string(reg) + ";2;" + std::to_string(percent(static_cast<int>((c >> 16) & 0xff))) + ";" +
           std::to_string(percent(static_cast<int>((c >> 8) & 0xff))) + ";" + std::to_string(percent(static_cast<int>(c & 0xff)));
  }
  const size_t columns = static_cast<size_t>(width);
  std::vector<int> slot(palette_size, -1);
  std::vector<int> in_band;
  std::vector<uint8_t> planes;
  for (int band = 0; band < height; band += 6) {
    in_band.clear();
    planes.clear();
    for (int y = band; y < std::min(band + 6, height); y++) {
      const int * row = &regs[static_cast<size_t>(y) * columns];
      for (size_t x = 0; x < columns; x++) {
        int reg = row[x];
        if (reg < 0) continue;
        int & s = slot[static_cast<size_t>(reg)];
        if (s < 0) {
          s = static_cast<int>(in_band.size());
          in_band.push_back(reg);
          planes.resize(planes.size() + columns, 0);
        }
        planes[static_cast<size_t>(s) * columns + x] = static_cast<uint8_t>(planes[static_cast<size_t>(s) * columns + x] | (1 << (y - band)));
      }
    }
    std::vector<int> order = in_band;
    std::sort(order.begin(), order.end());
    bool first = true;
    for (int reg : order) {
      const uint8_t * bits = &planes[static_cast<size_t>(slot[static_cast<size_t>(reg)]) * columns];
      // Trailing empty columns need not be sent.
      int last = width - 1;
      while (last >= 0 && bits[last] == 0) last--;
      if (!first) out += '$';
      first = false;
      out += "#" + std::to_string(reg);
      for (int x = 0; x <= last;) {
        int run = 1;
        while (x + run <= last && bits[x + run] == bits[x]) run++;
        char c = static_cast<char>(0x3f + bits[x]);
        if (run > 2) out += "!" + std::to_string(run) + c;
        else out.append(static_cast<size_t>(run), c);
        x += run;
      }
    }
    for (int reg : in_band) slot[static_cast<size_t>(reg)] = -1;
    out += '-';
  }
  out += "\x1b\\";
  return out;
}

}  // namespace

std::string encodeSixel(const Image & image, int x0, int y0, int width, int height, const std::vector<bool> & keep, const Dither & dither,
                        int registers) {
  return encodeSixelWith(makeSixelPalette(image, x0, y0, width, height, keep, registers, true, &dither), image, x0, y0, width, height, keep, dither);
}

int pictureColours(PictureQuality quality) {
  switch (quality) {
    case PictureQuality::REDUCED: return 64;
    case PictureQuality::LOW: return 16;
    default: return 0;
  }
}

PictureQuality pictureQualityFor(LinkClass cls) {
  switch (cls) {
    case LinkClass::FULL:
    case LinkClass::HZ_30: return PictureQuality::FULL;
    case LinkClass::HZ_15: return PictureQuality::REDUCED;
    default: return PictureQuality::LOW;
  }
}

namespace {

// The picture's pixels as register numbers in a palette made from it.
struct Indexed {
  SixelPalette palette;
  std::vector<int> regs;  // one per pixel
};

Indexed indexPicture(const Image & image, int x, int y, int width, int height, int colours, bool dithered) {
  Indexed out;
  std::vector<bool> all(static_cast<size_t>(width) * static_cast<size_t>(height), true);
  out.palette = makeSixelPalette(image, x, y, width, height, all, colours);
  bool transparent = false;
  Dither dither;  // fixed to the picture, so the same picture is the same bytes
  dither.method = dithered ? Dither::Method::ORDERED : Dither::Method::NEAREST;
  out.regs = mapToRegisters(out.palette, image, x, y, width, height, all, dither, transparent);
  // A pixel too transparent to be drawn takes the first register: a palette PNG here has no alpha.
  for (int & reg : out.regs) {
    if (reg < 0) reg = 0;
  }
  if (out.palette.rgb.empty()) out.palette.rgb.push_back(0);
  return out;
}

}  // namespace

std::vector<uint8_t> encodePalettePng(const Image & image, int x0, int y0, int width, int height, int colours, bool dither) {
  StageTimer timer(Stage::PNG);
  Indexed picture = indexPicture(image, x0, y0, width, height, std::min(colours, 256), dither);
  int bits = picture.palette.rgb.size() <= 16 ? 4 : 8;
  size_t row_bytes = bits == 4 ? (static_cast<size_t>(width) + 1) / 2 : static_cast<size_t>(width);
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(height) * (row_bytes + 1));
  for (int y = 0; y < height; y++) {
    raw.push_back(0);  // filter type 0 (none)
    for (int x = 0; x < width; x += (bits == 4 ? 2 : 1)) {
      uint8_t a = static_cast<uint8_t>(picture.regs[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)]);
      if (bits == 8) {
        raw.push_back(a);
      } else {
        uint8_t b = x + 1 < width ? static_cast<uint8_t>(picture.regs[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x) + 1]) : 0;
        raw.push_back(static_cast<uint8_t>((a << 4) | b));
      }
    }
  }
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  std::vector<uint8_t> ihdr;
  put32(ihdr, static_cast<uint32_t>(width));
  put32(ihdr, static_cast<uint32_t>(height));
  ihdr.insert(ihdr.end(), {static_cast<uint8_t>(bits), 3, 0, 0, 0});  // indexed colour, no interlace
  chunk(png, "IHDR", ihdr);
  std::vector<uint8_t> plte;
  for (uint32_t c : picture.palette.rgb) {
    plte.push_back(static_cast<uint8_t>((c >> 16) & 0xff));
    plte.push_back(static_cast<uint8_t>((c >> 8) & 0xff));
    plte.push_back(static_cast<uint8_t>(c & 0xff));
  }
  chunk(png, "PLTE", plte);
  chunk(png, "IDAT", zlibCompress(raw));
  chunk(png, "IEND", {});
  return png;
}

std::vector<uint8_t> quantisedRgb(const Image & image, int colours, bool dither) {
  Indexed picture = indexPicture(image, 0, 0, image.width, image.height, colours, dither);
  std::vector<uint8_t> rgb;
  rgb.reserve(picture.regs.size() * 3);
  for (int reg : picture.regs) {
    uint32_t c = picture.palette.rgb[static_cast<size_t>(reg)];
    rgb.push_back(static_cast<uint8_t>((c >> 16) & 0xff));
    rgb.push_back(static_cast<uint8_t>((c >> 8) & 0xff));
    rgb.push_back(static_cast<uint8_t>(c & 0xff));
  }
  return rgb;
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

namespace {

// What the statistics call a placement's picture: the kind of a shared one, else its size.
std::string statLabel(const ImagePlacement & p) {
  if (p.shared.valid()) return p.shared.kind;
  return std::to_string(p.image->width) + "x" + std::to_string(p.image->height);
}

}  // namespace

// Sends the pixels of `img` as kitty image `id`: as they are, or at a lower quality a palette PNG
// (quantised RGB where the terminal took no PNG); for a small picture both are made and the smaller
// sent, as a PNG's headers and palette can outweigh what the colours save.
void ImageEncoder::uploadKitty(const Image & img, uint32_t id, std::string & out) {
  // What goes up: the pixels as they are, or at a lower quality a palette PNG (quantised RGB where the
  // terminal took no PNG); for a small picture both are made and the smaller sent, as a PNG's headers
  // and palette can outweigh what the colours save.
  struct Upload {
    std::vector<uint8_t> bytes;
    bool png = false, deflated = false;
  };
  auto fullRgb = [&]() {
    Upload u;
    u.bytes.reserve(img.pixels.size() * 3);
    for (uint32_t px : img.pixels) {
      u.bytes.push_back(Image::red(px));
      u.bytes.push_back(Image::green(px));
      u.bytes.push_back(Image::blue(px));
    }
    return u;
  };
  // Compressed when that saves enough to be worth the terminal inflating it.
  auto deflate = [&](Upload & u) {
    if (!compress_ || u.png) return;
    std::vector<uint8_t> z = zlibCompress(u.bytes);
    if (z.size() * 10 < u.bytes.size() * 9) {
      u.bytes = std::move(z);
      u.deflated = true;
    }
  };
  Upload up;
  int colours = pictureColours(quality_);
  if (colours > 0) {
    if (palette_png_) {
      up.bytes = encodePalettePng(img, 0, 0, img.width, img.height, colours, img.stretched());
      up.png = true;
    } else {
      up.bytes = quantisedRgb(img, colours, img.stretched());
      deflate(up);
    }
    if (img.pixels.size() <= kSmallPicturePixels) {
      Upload plain = fullRgb();
      deflate(plain);
      if (plain.bytes.size() < up.bytes.size()) up = std::move(plain);
    }
  } else {
    up = fullRgb();
    deflate(up);
  }
  bool as_png = up.png, deflated = up.deflated;
  std::vector<uint8_t> & rgb = up.bytes;
  if (as_png) png_ids_.insert(id);
  if (deflated) deflated_ids_.insert(id);
  std::string data = base64(rgb);
  for (size_t i = 0; i < data.size() || i == 0; i += kKittyChunk) {
    bool more = i + kKittyChunk < data.size();
    size_t chunk_start = out.size();
    out += "\x1b_G";
    if (i == 0 && as_png) out += "a=t,f=100,i=" + std::to_string(id) + ",";
    else if (i == 0) out += std::string(deflated ? "o=z," : "") + "a=t,f=24,s=" + std::to_string(img.width) + ",v=" + std::to_string(img.height) + ",i=" + std::to_string(id) + ",";
    out += "q=1,m=" + std::string(more ? "1" : "0") + ";";
    out += data.substr(i, kKittyChunk);
    out += "\x1b\\";
    sizes_.upload += out.size() - chunk_start;
    if (data.empty()) break;
  }
}

// A placement of a shared picture: the pixels go up once, under one id for every placement of the picture,
// and each placement is a `a=p` of its own (a placement id apart from the others of that picture).
std::string ImageEncoder::kittySharedPlacement(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared) {
  std::string out;
  const Image & img = *p.image;
  SharedUpload & upload = shared_uploads_[img.version];
  size_t upload_before = sizes_.upload;
  if (upload.id == 0) {
    if (++next_id_ == 0) next_id_ = 1;
    upload.id = next_id_;
    upload.bytes = static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4;
    uploadKitty(img, upload.id, out);
    picture_stats_[statLabel(p)].uploads++;
    picture_stats_[statLabel(p)].bytes += sizes_.upload - upload_before;
  }
  upload.last_used = ++tick_;
  if (!shown.shared && shown.id) {
    // This place showed a picture of its own and shows a shared one now: free the old.
    deflated_ids_.erase(shown.id);
    png_ids_.erase(shown.id);
    std::string del = "\x1b_Ga=d,d=I,i=" + std::to_string(shown.id) + ",q=1\x1b\\";
    sizes_.placement += del.size();
    out += del;
    shown = Shown{};
    fresh = true;
  }
  bool changed = fresh || shown.version != img.version;
  constexpr uint32_t kPlacementsPerPicture = 256;
  std::vector<Rect> rects = visibleRects(frame, p, false);
  if (changed || cleared || rects != shown.rects || p.row != shown.row || p.col != shown.col) {
    if (shown.place_base == 0) shown.place_base = (next_place_ += kPlacementsPerPicture);
    // The placements of the picture this one showed before, now another.
    if (changed && shown.id && shown.id != upload.id) {
      for (size_t k = 0; k < shown.rects.size(); k++) {
        size_t before = out.size();
        out += "\x1b_Ga=d,d=i,i=" + std::to_string(shown.id) + ",p=" + std::to_string(shown.place_base + k + 1) + ",q=1\x1b\\";
        sizes_.placement += out.size() - before;
      }
    }
    int ch = profile_.cell_px_height, cw = profile_.cell_px_width;
    for (size_t k = 0; k < rects.size() && k < kPlacementsPerPicture - 1; k++) {
      const Rect & r = rects[k];
      int x0, x1, y0, y1;
      img.pixelSpan(r.col - p.col, r.cols, cw, true, x0, x1);
      img.pixelSpan(r.row - p.row, r.rows, ch, false, y0, y1);
      if (x1 <= x0 || y1 <= y0) continue;
      size_t place_start = out.size();
      out += cup(r.row, r.col);
      out += "\x1b_Ga=p,i=" + std::to_string(upload.id) + ",p=" + std::to_string(shown.place_base + k + 1) + ",x=" + std::to_string(x0) +
             ",y=" + std::to_string(y0) + ",w=" + std::to_string(x1 - x0) + ",h=" + std::to_string(y1 - y0);
      if (img.stretched()) out += ",c=" + std::to_string(r.cols) + ",r=" + std::to_string(r.rows);
      out += ",C=1,q=1\x1b\\";
      sizes_.placement += out.size() - place_start;
    }
    // Placements this picture no longer has (the data stays: other places show it).
    if (!changed && !cleared) {
      for (size_t k = rects.size(); k < shown.rects.size(); k++) {
        size_t before = out.size();
        out += "\x1b_Ga=d,d=i,i=" + std::to_string(upload.id) + ",p=" + std::to_string(shown.place_base + k + 1) + ",q=1\x1b\\";
        sizes_.placement += out.size() - before;
      }
    }
    shown.rects = rects;
    shown.row = p.row;
    shown.col = p.col;
  }
  shown.shared = true;
  shown.id = upload.id;
  shown.version = img.version;
  return out;
}

// Frees the shared pictures nothing shows, the longest unplaced first, while the terminal would hold more
// than the budget.
void ImageEncoder::evictSharedUploads(const std::set<uint32_t> & in_use, std::string & out) {
  size_t total = 0;
  for (const auto & [version, upload] : shared_uploads_) total += upload.bytes;
  while (total > shared_budget_) {
    auto oldest = shared_uploads_.end();
    for (auto it = shared_uploads_.begin(); it != shared_uploads_.end(); ++it) {
      if (in_use.count(it->second.id)) continue;
      if (oldest == shared_uploads_.end() || it->second.last_used < oldest->second.last_used) oldest = it;
    }
    if (oldest == shared_uploads_.end()) return;
    std::string del = "\x1b_Ga=d,d=I,i=" + std::to_string(oldest->second.id) + ",q=1\x1b\\";
    sizes_.placement += del.size();
    out += del;
    deflated_ids_.erase(oldest->second.id);
    png_ids_.erase(oldest->second.id);
    total -= oldest->second.bytes;
    shared_uploads_.erase(oldest);
  }
}

std::string ImageEncoder::kitty(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared) {
  if (p.shared.valid()) return kittySharedPlacement(frame, p, shown, fresh, cleared);
  std::string out;
  if (shown.shared) {
    // This place showed a shared picture and shows its own now: leave the shared one to its other places.
    for (size_t k = 0; k < shown.rects.size(); k++) {
      size_t before = out.size();
      out += "\x1b_Ga=d,d=i,i=" + std::to_string(shown.id) + ",p=" + std::to_string(shown.place_base + k + 1) + ",q=1\x1b\\";
      sizes_.placement += out.size() - before;
    }
    shown = Shown{};
    fresh = true;
  }
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
    uploadKitty(img, shown.id, out);
    picture_stats_[statLabel(p)].uploads++;
    picture_stats_[statLabel(p)].bytes += sizes_.upload - upload_before;
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
    deflated_ids_.erase(old_id);
    png_ids_.erase(old_id);
    std::string del = "\x1b_Ga=d,d=I,i=" + std::to_string(old_id) + ",q=1\x1b\\";
    sizes_.placement += del.size();
    out += del;
  }
  return out;
}

namespace {

// Whether any pixel of the rectangle differs between the two pictures (of one size).
bool pixelsDiffer(const Image & a, const Image & b, int x, int y, int w, int h) {
  for (int row = y; row < y + h; row++) {
    size_t at = static_cast<size_t>(row) * static_cast<size_t>(a.width) + static_cast<size_t>(x);
    if (!std::equal(a.pixels.begin() + static_cast<std::ptrdiff_t>(at), a.pixels.begin() + static_cast<std::ptrdiff_t>(at) + w,
                    b.pixels.begin() + static_cast<std::ptrdiff_t>(at))) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::string ImageEncoder::sixel(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared) {
  std::vector<Rect> rects = visibleRects(frame, p, true);
  bool changed = fresh || cleared || shown.version != p.image->version || rects != shown.rects || p.row != shown.row || p.col != shown.col;
  // Only the picture changed, in the same place and size: the cells that differ are enough.
  bool same_place = !fresh && !cleared && rects == shown.rects && p.row == shown.row && p.col == shown.col;
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
  size_t upload_before = sizes_.upload;
  bool diff = same_place && shown.sent.width == img.width && shown.sent.height == img.height && shown.sent.pixels.size() == img.pixels.size();
  std::string out;
  if (!sixel_ready_) {
    out += profile_.sixel_setup;
    sixel_ready_ = true;
  }
  sizes_.placement += out.size();
  struct Piece {
    std::string bytes;
    size_t position = 0, picture = 0;  // of `bytes`, the cursor positions and the Sixel data
  };
  const bool palette_mix = std::getenv("CELLUI_BLUR_MIX") && p.image->stretched() && quality_ == PictureQuality::FULL;
  auto piece = [&](Piece & into, const SixelPalette & palette, int x, int y, int w, int h, int row, int col) {
    std::vector<bool> keep(static_cast<size_t>(w) * static_cast<size_t>(h), true);
    std::string place = cup(row, col);
    into.position += place.size();
    into.bytes += place;
    Dither dither;
    // Dithering is noise to a chart and costs bytes: below full quality only a stretched picture (a blurred
    // backdrop, a gradient) is dithered, as it would band.
    dither.method = quality_ == PictureQuality::FULL || p.image->stretched() ? profile_.dither : Dither::Method::NEAREST;
    dither.origin_x = p.col * cw + x;
    dither.origin_y = p.row * ch + y;
    dither.mix = palette_mix;
    std::string data = encodeSixelWith(palette, img, x, y, w, h, keep, dither);
    into.picture += data.size();
    into.bytes += data;
  };
  auto emit = [&](Piece & from) {
    out += from.bytes;
    sizes_.placement += from.position;
    sizes_.upload += from.picture;
    picture_stats_[statLabel(p)].bytes += from.picture + from.position;
  };
  for (const Rect & r : rects) {
    // One Sixel to a rectangle: what lies between two of them (a popup over a chart) is not sent.
    int x = (r.col - p.col) * cw, y = (r.row - p.row) * ch;
    int w = std::min(r.cols * cw, img.width - x), h = std::min(r.rows * ch, img.height - y);
    if (w <= 0 || h <= 0) continue;
    // Which cells of the rectangle changed.
    std::vector<char> dirty(static_cast<size_t>(r.rows) * static_cast<size_t>(r.cols), 1);
    size_t dirty_count = dirty.size();
    if (diff) {
      dirty_count = 0;
      for (int tr = 0; tr < r.rows; tr++) {
        for (int tc = 0; tc < r.cols; tc++) {
          int tx = x + tc * cw, ty = y + tr * ch;
          int tw = std::min(cw, x + w - tx), th = std::min(ch, y + h - ty);
          bool d = tw > 0 && th > 0 && pixelsDiffer(img, shown.sent, tx, ty, tw, th);
          dirty[static_cast<size_t>(tr) * static_cast<size_t>(r.cols) + static_cast<size_t>(tc)] = d;
          dirty_count += d ? 1 : 0;
        }
      }
      if (dirty_count == 0) continue;
    }
    // One palette for the rectangle, so the pieces sent are drawn in the colours of the pieces not sent.
    std::vector<bool> all(static_cast<size_t>(w) * static_cast<size_t>(h), true);
    int registers = pictureColours(quality_) > 0 ? std::min(profile_.sixel_registers, pictureColours(quality_)) : profile_.sixel_registers;
    // The same dither as the pieces', for the rectangle's own origin.
    Dither palette_dither;
    palette_dither.method = quality_ == PictureQuality::FULL || p.image->stretched() ? profile_.dither : Dither::Method::NEAREST;
    palette_dither.origin_x = p.col * cw + x;
    palette_dither.origin_y = p.row * ch + y;
    palette_dither.gradient = p.image->stretched();
    // A blurred backdrop (stretched over its cells) at full quality goes out in the terminal's default
    // palette where it has the registers for it: quicker than one made from the picture, in duller colours.
    bool fixed = p.image->stretched() && quality_ == PictureQuality::FULL && profile_.sixel_registers_total >= kXtermRegisters;
    // EXPERIMENT: CELLUI_BLUR_REGISTERS=N draws the blur in a palette of N registers made from each tile.
    if (const char * blur_registers = std::getenv("CELLUI_BLUR_REGISTERS")) {
      if (p.image->stretched() && quality_ == PictureQuality::FULL) {
        fixed = false;
        registers = std::max(2, std::atoi(blur_registers));
      }
    }
    if (std::getenv("CELLUI_BLUR_MIX") && p.image->stretched() && quality_ == PictureQuality::FULL) {
      palette_dither.mix = true;
      palette_dither.gradient = false;
      fixed = false;
      if (!std::getenv("CELLUI_BLUR_REGISTERS")) registers = 48;
    }
    bool on_grid = !(std::getenv("CELLUI_BLUR_GRID") && std::atoi(std::getenv("CELLUI_BLUR_GRID")) == 0 && p.image->stretched());
    SixelPalette palette = fixed ? xtermPalette() : makeSixelPalette(img, x, y, w, h, all, registers, on_grid, &palette_dither);
    // The cells to send are a row of them at a time, from the first that changed to the last: each Sixel
    // carries the definitions of its registers, which cost more than the cells between two changes.
    std::vector<std::pair<int, int>> spans(static_cast<size_t>(r.rows), {r.cols, -1});
    size_t span_cells = 0;
    for (int tr = 0; tr < r.rows; tr++) {
      for (int tc = 0; tc < r.cols; tc++) {
        if (dirty[static_cast<size_t>(tr) * static_cast<size_t>(r.cols) + static_cast<size_t>(tc)]) {
          spans[static_cast<size_t>(tr)].first = std::min(spans[static_cast<size_t>(tr)].first, tc);
          spans[static_cast<size_t>(tr)].second = tc;
        }
      }
      if (spans[static_cast<size_t>(tr)].second >= 0) span_cells += static_cast<size_t>(spans[static_cast<size_t>(tr)].second + 1 - spans[static_cast<size_t>(tr)].first);
    }
    Piece whole;
    if (!diff || span_cells * 100 > dirty.size() * kPartialBelowPercent) {
      // Much of it: one Sixel is cheaper than a header and a position for each row.
      piece(whole, palette, x, y, w, h, r.row, r.col);
      emit(whole);
      continue;
    }
    Piece rows;
    for (int tr = 0; tr < r.rows; tr++) {
      auto [first, last] = spans[static_cast<size_t>(tr)];
      if (last < 0) continue;
      int rx = x + first * cw, ry = y + tr * ch;
      int rw = std::min((last + 1 - first) * cw, x + w - rx), rh = std::min(ch, y + h - ry);
      if (rw > 0 && rh > 0) piece(rows, palette, rx, ry, rw, rh, r.row + tr, r.col + first);
    }
    // Whichever is smaller: the rows that changed, or all of it.
    piece(whole, palette, x, y, w, h, r.row, r.col);
    emit(rows.bytes.size() < whole.bytes.size() ? rows : whole);
  }
  shown.sent = img;
  if (sizes_.upload > upload_before) picture_stats_[statLabel(p)].uploads++;
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
    int colours = pictureColours(quality_);
    std::vector<uint8_t> png = colours > 0 ? encodePalettePng(img, x0, y0, x1 - x0, y1 - y0, colours, img.stretched()) : encodePng(img, x0, y0, x1 - x0, y1 - y0);
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
    picture_stats_[statLabel(p)].uploads++;
    picture_stats_[statLabel(p)].bytes += out.size() - osc_start;
  }
  return out;
}

std::string ImageEncoder::encode(const CellBuffer & frame, const std::vector<ImagePlacement> & images, bool cleared) {
  StageTimer timer(Stage::IMAGE_ENCODE);
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
      if (it->second.shared) {
        // Only the placements: the picture stays for the next place that shows it.
        for (size_t k = 0; k < it->second.rects.size(); k++) {
          std::string del = "\x1b_Ga=d,d=i,i=" + std::to_string(it->second.id) + ",p=" + std::to_string(it->second.place_base + k + 1) + ",q=1\x1b\\";
          sizes_.placement += del.size();
          out += del;
        }
      } else {
        std::string del = "\x1b_Ga=d,d=I,i=" + std::to_string(it->second.id) + ",q=1\x1b\\";
        sizes_.placement += del.size();
        out += del;
      }
    }
    it = shown_.erase(it);
  }
  if (profile_.protocol == PixelProtocol::KITTY && !shared_uploads_.empty()) {
    std::set<uint32_t> in_use;
    for (const auto & [key, shown] : shown_) {
      if (shown.shared) in_use.insert(shown.id);
    }
    evictSharedUploads(in_use, out);
  }
  return out;
}

std::string ImageEncoder::handleKittyReply(const std::string & body) {
  size_t semi = body.find(';');
  std::string message = semi == std::string::npos ? "" : body.substr(semi + 1);
  if (message == "OK") return "";
  // (a reply to a compressed upload that is not an error never comes: q=1)
  std::string keys = body.substr(0, semi);
  uint32_t id = 0;
  size_t at = keys.find("i=");
  if (at != std::string::npos) id = static_cast<uint32_t>(std::strtoul(keys.c_str() + at + 2, nullptr, 10));
  // A refused shared upload is made again by the next placement that shows it.
  auto forgetShared = [&]() {
    for (auto it = shared_uploads_.begin(); it != shared_uploads_.end();) {
      if (it->second.id == id) it = shared_uploads_.erase(it);
      else ++it;
    }
  };
  if (id && png_ids_.erase(id) && palette_png_) {
    forgetShared();
    // The terminal refused a palette PNG: quantised RGB from now on, and the refused picture again.
    palette_png_ = false;
    for (auto & [key, shown] : shown_) {
      if (shown.id == id) shown.version = ~uint64_t{0};
    }
  } else if (id && deflated_ids_.erase(id) && compress_) {
    forgetShared();
    // The terminal refused a compressed upload: send raw from now on, and the refused pictures again.
    compress_ = false;
    for (auto & [key, shown] : shown_) {
      if (shown.id == id) shown.version = ~uint64_t{0};
    }
  }
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
  for (const auto & [version, upload] : shared_uploads_) {
    if (profile_.protocol == PixelProtocol::KITTY) out += "\x1b_Ga=d,d=I,i=" + std::to_string(upload.id) + ",q=1\x1b\\";
  }
  shared_uploads_.clear();
  shown_.clear();
  return out;
}

}  // namespace cellui::term
