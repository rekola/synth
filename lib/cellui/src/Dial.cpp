#include <cellui/Dial.h>

#include <cellui/Utf8.h>

#include <algorithm>
#include <array>
#include <map>
#include <tuple>

namespace cellui {

void drawCellDial(Plane & plane, int row, int col, int rows, int cols, const DialReading & reading, const DialLook & look, int clip_top,
                  int clip_bottom) {
  if (rows <= 0 || cols <= 0) return;
  plane.setFgColor(look.foreground);
  plane.setBgColor(look.background);
  // The letter centred over the value, the value at the right.
  for (int i = 0; i < rows; i++) {
    int y = row + i;
    if (y < clip_top || y >= clip_bottom) continue;
    std::string text;
    if (i == 0) {
      int pad = std::max(0, (cols - utf8::displayWidth(reading.letter)) / 2);
      text = utf8::padToWidth(std::string(static_cast<size_t>(pad), ' ') + reading.letter, cols);
    } else if (i == rows - 1) {
      auto shown = utf8::truncateToWidth(reading.text, cols);
      text = std::string(static_cast<size_t>(std::max(0, cols - utf8::displayWidth(shown))), ' ') + shown;
    } else {
      text = std::string(static_cast<size_t>(cols), ' ');
    }
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
