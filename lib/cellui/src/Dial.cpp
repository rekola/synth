#include <cellui/Dial.h>

#include <cellui/Utf8.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

float Dial::fraction() const { return std::clamp((value_ - spec_.min) / (spec_.max - spec_.min), 0.0f, 1.0f); }

std::string Dial::valueText() const {
  if (value_ < spec_.min - 1.0f) return "off";
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.0f", static_cast<double>(value_));
  return buf;
}

void Dial::draw() {
  const StyleProvider & styles = plane_.getStyles();
  // Only the lines change colour: the cursor colour while selected, a brighter one while pressed.
  Color fg = foreground_.blend(std::clamp(selected_, 0.0f, 1.0f), styles.highlight_bg_color);
  if (pressed_) fg = styles.highlight_bg_color.blend(0.45f, Color(255, 255, 255));
  drawDial(plane_, row_, col_, kRows, kCols, DialReading{spec_.letter, fraction(), valueText()}, DialLook{background_, fg}, clip_top_,
           clip_bottom_, id_);
}

bool Dial::contains(int screen_y, int screen_x) const {
  int y = screen_y - plane_.getPosition().first, x = screen_x - plane_.getPosition().second;
  return y >= row_ && y < row_ + kRows && x >= col_ && x < col_ + kCols && y >= clip_top_ && y < clip_bottom_;
}

void Dial::turn(float base, float delta) {
  // A value above the arc's top stays where it is rather than dropping to it.
  float top = std::max(spec_.max, base);
  float value = std::clamp(base + delta, spec_.min - 1.0f, top);
  if (value < spec_.min) value = spec_.off;
  if (std::abs(value - value_) < 0.001f) return;
  value_ = value;
  if (on_change) on_change(value);
}

void Dial::startEdit() {
  InlineEditor::Field field;
  field.row = row_ + kRows / 2;
  field.col = col_;
  field.width = kEditWidth;
  if (value_ < spec_.off + 1.0f) {
    field.initial_text = "-inf " + spec_.unit;
  } else {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", std::round(static_cast<double>(value_) * 10.0) / 10.0);
    field.initial_text = buf + std::string(" ") + spec_.unit;
  }
  field.replace_on_type = true;
  editor_.open(field, [this](std::string text) {
    char * end = nullptr;
    float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str()) return;  // not a number: the value stays
    value = std::clamp(value, spec_.typed_min, spec_.typed_max);
    value_ = value;
    if (on_change) on_change(value);
  });
}

bool Dial::offerInput(const Input & input) { return editor_.offerInput(input); }

bool Dial::offerMouse(const Input & input) {
  if (editor_.isOpen()) return false;
  bool fine = input.hasShift();
  if (input.getId() == Key::Button4 || input.getId() == Key::Button5) {
    if (input.getKind() == Input::Kind::RELEASE || !contains(input.getY(), input.getX())) return false;
    float up = input.getId() == Key::Button4 ? 1.0f : -1.0f;
    turn(std::max(value_, spec_.min), up * spec_.wheel_step * (fine ? spec_.fine : 1.0f));
    return true;
  }
  if (input.getId() != Key::Button1) return false;
  if (input.getKind() == Input::Kind::RELEASE) {
    if (!down_) return false;
    bool dragged = moved_;
    down_ = pressed_ = moved_ = false;
    if (dragged) {
      if (on_turn_end) on_turn_end();
    } else {
      if (on_click) on_click();
      startEdit();
    }
    return true;
  }
  float y = input.getFy() - static_cast<float>(plane_.getPosition().first);
  if (!down_) {
    // A fresh press, over the dial.
    if (!contains(input.getY(), input.getX())) return false;
    down_ = pressed_ = true;
    moved_ = false;
    start_y_ = y;
    start_value_ = std::max(value_, spec_.min);
    if (value_ > spec_.max) start_value_ = value_;
    return true;
  }
  // The press again, at a new position: a drag, up raising the value.
  float rows_up = start_y_ - y;
  if (!moved_ && std::abs(rows_up) >= 0.5f) {
    moved_ = true;
    if (on_turn_begin) on_turn_begin();
  }
  if (moved_) turn(start_value_, rows_up * spec_.drag_per_row * (fine ? spec_.fine : 1.0f));
  return true;
}

}  // namespace cellui
