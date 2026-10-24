#include <cellui/Dial.h>

#include <cellui/Utf8.h>

#include <algorithm>
#include <array>
#include <map>
#include <tuple>

namespace cellui {

void drawCellDial(Plane & plane, int row, int col, int rows, int cols, const DialReading & reading, const DialLook & look, int clip_top,
                  int clip_bottom) {
  if (rows < 3 || cols < 3) return;
  plane.setFgColor(look.foreground);
  plane.setBgColor(look.background);
  // A rounded box: the value in the middle, the letter in the bottom border.
  auto inner = cols - 2;
  auto centred = [&](const std::string & text, const char * fill) {
    auto shown = utf8::truncateToWidth(text, inner);
    int left = (inner - utf8::displayWidth(shown)) / 2, right = inner - utf8::displayWidth(shown) - left;
    std::string out;
    for (int i = 0; i < left; i++) out += fill;
    out += shown;
    for (int i = 0; i < right; i++) out += fill;
    return out;
  };
  auto line = [&](const char * left, const std::string & middle, const char * right) { return std::string(left) + middle + right; };
  for (int i = 0; i < rows; i++) {
    int y = row + i;
    if (y < clip_top || y >= clip_bottom) continue;
    std::string text;
    if (i == 0) text = line("╭", centred("", "─"), "╮");
    else if (i == rows - 1) text = line("╰", centred(reading.letter, "─"), "╯");
    else if (i == rows / 2) text = line("│", centred(reading.text, " "), "│");
    else text = line("│", centred("", " "), "│");
    plane.putstr(y, col, text);
  }
}

void drawDial(Plane & plane, int row, int col, int rows, int cols, const DialReading & reading, const DialLook & look, int clip_top,
              int clip_bottom, uint64_t id) {
  int cell_h = 0, cell_w = 0;
  if (rows <= 0 || cols <= 0 || std::max(row, clip_top) >= std::min(row + rows, clip_bottom)) return;
  if (!plane.cellPixelSize(cell_h, cell_w)) {
    drawCellDial(plane, row, col, rows, cols, reading, look, clip_top, clip_bottom);
    return;
  }
  unsigned width = static_cast<unsigned>(cols * cell_w), height = static_cast<unsigned>(rows * cell_h);
  float scale = static_cast<float>(plane.displayScale());
  // The frame redraws far more often than a dial changes: keep the last
  // pictures by what they show.
  using Key = std::tuple<std::string, int, uint32_t, uint32_t, uint32_t, unsigned, unsigned, int>;
  auto quantized = static_cast<int>(std::clamp(reading.fraction, 0.0f, 1.0f) * 1024.0f + 0.5f);
  auto fg = look.foreground.toSrgb8(), bg = look.background.toSrgb8();
  auto pack = [](const std::array<uint8_t, 3> & c) { return static_cast<uint32_t>(c[0]) << 16 | static_cast<uint32_t>(c[1]) << 8 | c[2]; };
  Key key{reading.letter, quantized, pack(fg), pack(bg), 0, width, height, static_cast<int>(scale * 100.0f)};
  static thread_local std::map<Key, std::vector<uint32_t>> cache;
  auto found = cache.find(key);
  if (found == cache.end()) {
    if (cache.size() > 512) cache.clear();
    DialReading quantized_reading = reading;
    quantized_reading.fraction = static_cast<float>(quantized) / 1024.0f;
    found = cache.emplace(key, dial_image::render(quantized_reading, look, width, height, scale)).first;
  }
  if (found->second.empty()) {  // canvas has no pixel backend
    drawCellDial(plane, row, col, rows, cols, reading, look, clip_top, clip_bottom);
    return;
  }
  plane.drawImageAt(row, col, found->second, static_cast<int>(width), static_cast<int>(height), id, clip_top, clip_bottom);
}

}  // namespace cellui
