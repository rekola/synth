#include <cellui/Dial.h>

#include <cellui/Utf8.h>

#include <cellui/NumberParse.h>

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
    else if (i == rows / 2) {
      if (reading.editing) {
        // The tail of what is typed, with the caret, if it is too long.
        std::string shown = reading.edit_text + "▏";
        while (utf8::displayWidth(shown) > inner && shown.size() > 3) shown.erase(0, 1);
        text = line("│", centred(shown, " "), "│");
      } else {
        text = line("│", centred(reading.text, " "), "│");
      }
    }
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
  // The picture depends only on what it shows, so it is shared by key: drawn once for the whole tree,
  // and dials that show the same (every send of a track at its default, say) are one picture, which a
  // terminal that stores pictures takes once.
  auto quantized = static_cast<int>(std::clamp(reading.fraction, 0.0f, 1.0f) * 1024.0f + 0.5f);
  auto fg = look.foreground.toSrgb8(), bg = look.background.toSrgb8();
  auto pack = [](const std::array<uint8_t, 3> & c) { return static_cast<uint64_t>(c[0]) << 16 | static_cast<uint64_t>(c[1]) << 8 | c[2]; };
  uint64_t hash = 14695981039346656037ull;
  auto mix = [&](uint64_t v) { hash = (hash ^ v) * 1099511628211ull; };
  for (char c : reading.letter) mix(static_cast<uint64_t>(static_cast<unsigned char>(c)));
  mix(static_cast<uint64_t>(quantized));
  mix(pack(fg));
  mix(pack(bg));
  mix(reading.editing ? 1u : 0u);
  mix(static_cast<uint64_t>(static_cast<int>(scale * 100.0f)));
  if (reading.editing) {
    for (char c : reading.edit_text) mix(static_cast<uint64_t>(static_cast<unsigned char>(c)));
  }
  bool no_pixels = false;
  plane.drawSharedImageAt(
    row, col, ImageKey{"dial", hash}, static_cast<int>(width), static_cast<int>(height),
    [&]() {
      DialReading quantized_reading = reading;
      quantized_reading.fraction = static_cast<float>(quantized) / 1024.0f;
      std::vector<uint32_t> pixels = dial_image::render(quantized_reading, look, width, height, scale);
      no_pixels = pixels.empty();
      return pixels;
    },
    clip_top, clip_bottom);
  if (no_pixels) drawCellDial(plane, row, col, rows, cols, reading, look, clip_top, clip_bottom);  // canvas has no pixel backend
  (void)id;
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
  DialReading reading{spec_.letter, fraction(), valueText()};
  reading.editing = editing_;
  reading.edit_text = edit_text_;
  drawDial(plane_, row_, col_, kRows, kCols, reading, DialLook{background_, fg}, clip_top_, clip_bottom_, id_);
}

bool Dial::contains(int screen_y, int screen_x) const {
  int y = screen_y - plane_.getPosition().first, x = screen_x - plane_.getPosition().second;
  return y >= row_ && y < row_ + kRows && x >= col_ && x < col_ + kCols && y >= clip_top_ && y < clip_bottom_;
}

float Dial::snap(float value) const {
  if (spec_.step <= 0.0f) return value;
  float top = std::floor((spec_.max - spec_.min) / spec_.step + 0.01f);
  float k = std::clamp(std::round((value - spec_.min) / spec_.step), 0.0f, top);
  return k < 0.5f ? spec_.off : spec_.min + k * spec_.step;
}

void Dial::turn(float base, float delta) {
  // A value above the arc's top stays where it is rather than dropping to it.
  float top = spec_.step > 0.0f ? spec_.max : std::max(spec_.max, base);
  float value = std::clamp(base + delta, spec_.min - 1.0f, top);
  if (spec_.step > 0.0f) value = snap(value);
  else if (value < spec_.min) value = spec_.off;
  if (std::abs(value - value_) < 0.001f) return;
  value_ = value;
  if (on_change) on_change(value);
}

void Dial::startEdit() {
  if (editing_) return;
  editing_ = replaceable_ = true;
  if (value_ < spec_.off + 1.0f) {
    edit_text_ = "off";
  } else {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", std::round(static_cast<double>(value_) * 10.0) / 10.0);
    edit_text_ = buf;
  }
  version_++;
}

void Dial::cancelEdit() {
  if (!editing_) return;
  editing_ = false;
  version_++;
}

void Dial::commitEdit() {
  editing_ = false;
  version_++;
  // Locale-independent, and a typed decimal comma counts as a point.
  std::string text = edit_text_;
  std::replace(text.begin(), text.end(), ',', '.');
  if (text == "off" || text == "-inf") {
    value_ = spec_.off;
    if (on_change) on_change(value_);
    return;
  }
  const char * begin = text.c_str();
  if (*begin == '+') begin++;  // "+3" is a fine thing to type
  float value = 0.0f;
  if (parseFloat(begin, text.c_str() + text.size(), value).ec != std::errc()) return;  // not a number: the value stays
  value_ = snap(std::clamp(value, spec_.typed_min, spec_.typed_max));
  if (on_change) on_change(value_);
}

bool Dial::key(int id, const Modifiers & mods) {
  if (!editing_) return false;
  if (id == Key::Enter) {
    commitEdit();
  } else if (id == Key::Escape || (mods.ctrl && id == 'g')) {
    cancelEdit();
  } else {
    bool printable = id >= 32 && id < 127 && !mods.ctrl && !mods.alt;
    if (replaceable_ && (printable || id == Key::Backspace || id == Key::Delete)) edit_text_.clear();
    replaceable_ = false;
    if (printable) edit_text_ += static_cast<char>(id);
    else if (id == Key::Backspace && !edit_text_.empty()) edit_text_.pop_back();
    else if (id == Key::Delete) edit_text_.clear();
    version_++;
  }
  return true;
}

bool Dial::wheel(int row, int col, bool up, bool shift) {
  if (editing_ || !contains(row, col)) return false;
  float base = std::max(value_, spec_.min);
  if (shift && spec_.step <= 0.0f) {
    turn(base, (up ? 1.0f : -1.0f) * spec_.wheel_step * spec_.fine);
  } else {
    // A plain step lands on a multiple of itself, so a level between steps (or with float noise)
    // does not carry its fraction through every step.
    float steps = base / spec_.wheel_step;
    float target = up ? std::floor(steps + 0.01f) + 1.0f : std::ceil(steps - 0.01f) - 1.0f;
    turn(target * spec_.wheel_step, 0.0f);
  }
  return true;
}

bool Dial::release() {
  if (editing_ || !down_) return false;
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

bool Dial::press(float fy, int row, int col, bool shift) {
  if (editing_) return false;
  float y = fy - static_cast<float>(plane_.getPosition().first);
  if (!down_) {
    // A fresh press, over the dial.
    if (!contains(row, col)) return false;
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
  if (moved_) turn(start_value_, rows_up * spec_.drag_per_row * (shift && spec_.step <= 0.0f ? spec_.fine : 1.0f));
  return true;
}

}  // namespace cellui
