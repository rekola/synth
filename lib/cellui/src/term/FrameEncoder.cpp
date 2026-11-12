#include <cellui/term/FrameEncoder.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>

namespace cellui::term {

namespace {

constexpr int kCubeLevels[6] = {0, 95, 135, 175, 215, 255};

int weightedDistance(int r1, int g1, int b1, int r2, int g2, int b2) {
  int dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
  return 2 * dr * dr + 4 * dg * dg + 3 * db * db;
}

int nearestLevel(int v) {
  int best = 0;
  for (int i = 1; i < 6; i++) {
    if (std::abs(kCubeLevels[i] - v) < std::abs(kCubeLevels[best] - v)) best = i;
  }
  return best;
}

// Whether terminals may disagree about how wide a cluster is: emoji
// presentation selectors, joiners, flags and the symbol blocks whose
// characters some terminals draw as emoji.
bool widthRisky(const std::string & text) {
  for (size_t i = 0; i < text.size();) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    size_t need = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    uint32_t cp = need == 0 ? c : c & (0x3Fu >> need);
    for (size_t k = 1; k <= need && i + k < text.size(); k++) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
    i += need + 1;
    if (cp == 0xFE0E || cp == 0xFE0F || cp == 0x200D) return true;
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return true;
    if (cp >= 0x1F000 && cp <= 0x1FAFF) return true;
    if (cp >= 0x2300 && cp <= 0x23FF) return true;
    if (cp >= 0x2600 && cp <= 0x27BF) return true;
  }
  return false;
}

bool sameColor(const std::optional<Color> & a, const std::optional<Color> & b) {
  if (a.has_value() != b.has_value()) return false;
  return !a || *a == *b || a->toSrgb8() == b->toSrgb8();
}

bool sameCell(const CellBuffer::Cell & a, const CellBuffer::Cell & b) {
  return a.text == b.text && a.continuation == b.continuation && a.image == b.image && a.bold() == b.bold() && a.italic == b.italic &&
         a.underline == b.underline && sameColor(a.fg, b.fg) && sameColor(a.bg, b.bg);
}

}  // namespace

int nearest256(const Color & c) {
  auto rgb = c.toSrgb8();
  int r = rgb[0], g = rgb[1], b = rgb[2];
  int ri = nearestLevel(r), gi = nearestLevel(g), bi = nearestLevel(b);
  int best = 16 + 36 * ri + 6 * gi + bi;
  int best_d = weightedDistance(r, g, b, kCubeLevels[ri], kCubeLevels[gi], kCubeLevels[bi]);
  for (int k = 0; k < 24; k++) {
    int v = 8 + 10 * k;
    int d = weightedDistance(r, g, b, v, v, v);
    if (d < best_d) {
      best_d = d;
      best = 232 + k;
    }
  }
  return best;
}

int nearest8(const Color & c) {
  auto rgb = c.toSrgb8();
  int r = rgb[0], g = rgb[1], b = rgb[2];
  if ((r >> 3) == (g >> 3) && (g >> 3) == (b >> 3)) return r < 64 ? 0 : 7;
  return (r >= 128 ? 1 : 0) | (g >= 128 ? 2 : 0) | (b >= 128 ? 4 : 0);
}

const Color & ansi16Color(int index) {
  static const Color table[16] = {
      Color("#000000"), Color("#cd0000"), Color("#00cd00"), Color("#cdcd00"), Color("#0000ee"), Color("#cd00cd"), Color("#00cdcd"), Color("#e5e5e5"),
      Color("#7f7f7f"), Color("#ff0000"), Color("#00ff00"), Color("#ffff00"), Color("#5c5cff"), Color("#ff00ff"), Color("#00ffff"), Color("#ffffff"),
  };
  return table[index & 15];
}

int nearest16(const Color & c) {
  auto rgb = c.toSrgb8();
  int best = 0, best_d = -1;
  for (int i = 0; i < 16; i++) {
    auto p = ansi16Color(i).toSrgb8();
    int d = weightedDistance(rgb[0], rgb[1], rgb[2], p[0], p[1], p[2]);
    if (best_d < 0 || d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

EncoderProfile EncoderProfile::from(const Capabilities & caps) {
  EncoderProfile p;
  p.colors = caps.colors;
  p.column_move = caps.column_move;
  p.sync = caps.sync;
  if (caps.distinct_default_bg) p.distinct_from = caps.display.default_bg;
  p.default_fg = caps.display.default_fg;
  p.default_bg = caps.display.default_bg;
  return p;
}

FrameEncoder::FrameEncoder(EncoderProfile profile) : profile_(std::move(profile)), full_(true) {
  clock_ = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
}

bool FrameEncoder::litBold(size_t index, const CellBuffer::Cell & cell, double now) {
  bool lit = cell.fg && std::max({ cell.fg->r(), cell.fg->g(), cell.fg->b() }) > kLitBold;
  double & since = lit_since_[index];
  if (!lit) {
    since = -1.0;
    return false;
  }
  if (since < 0.0) since = now;
  return now - since < kBoldHold;
}

FrameEncoder::Ink FrameEncoder::ink(const std::optional<Color> & c, bool background) const {
  Ink k;
  if (!c) return k;
  // At 16 colours and fewer the terminal's own text and background colours stay the terminal's.
  if (profile_.colors == ColorDepth::C8 || profile_.colors == ColorDepth::C16) {
    const std::optional<Color> & own = background ? profile_.default_bg : profile_.default_fg;
    if (own && c->toSrgb8() == own->toSrgb8()) return k;
  }
  switch (profile_.colors) {
    case ColorDepth::NONE:
      return k;
    case ColorDepth::C16:
      k.kind = Ink::INDEX;
      k.value = nearest16(*c);
      return k;
    case ColorDepth::C8:
      k.kind = Ink::INDEX;
      k.value = nearest8(*c);
      return k;
    case ColorDepth::C256:
      k.kind = Ink::INDEX;
      k.value = nearest256(*c);
      return k;
    case ColorDepth::TRUECOLOR:
      break;
  }
  k.kind = Ink::RGB;
  auto rgb = c->toSrgb8();
  k.value = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
  if (background && profile_.distinct_from) {
    auto d = profile_.distinct_from->toSrgb8();
    if (k.value == ((d[0] << 16) | (d[1] << 8) | d[2])) k.value ^= 1;
  }
  return k;
}

void FrameEncoder::moveTo(std::string & out, int y, int x) {
  if (cursor_y_ == y && cursor_x_ == x) return;
  if (cursor_y_ == y && profile_.column_move) {
    out += "\x1b[" + std::to_string(x + 1) + "G";
  } else {
    out += "\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H";
  }
  cursor_y_ = y;
  cursor_x_ = x;
}

void FrameEncoder::monoStyle(const CellBuffer::Cell & cell, bool & reverse, bool & dim) const {
  // The luma of a colour, 0 to 1 on its encoded values.
  auto luma = [](const Color & c) {
    auto rgb = c.toSrgb8();
    return (0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]) / 255.0;
  };
  double default_fg = profile_.default_fg ? luma(*profile_.default_fg) : 1.0;
  double default_bg = profile_.default_bg ? luma(*profile_.default_bg) : 0.0;
  // A background far from the terminal's own is a highlight: reverse video, whatever its colour.
  reverse = cell.bg && std::abs(luma(*cell.bg) - default_bg) > 0.3;
  // A grey text far from the terminal's own text colour is a quiet one.
  dim = false;
  if (!reverse && cell.fg) {
    auto rgb = cell.fg->toSrgb8();
    bool grey = std::max({rgb[0], rgb[1], rgb[2]}) - std::min({rgb[0], rgb[1], rgb[2]}) < 24;
    dim = grey && std::abs(luma(*cell.fg) - default_fg) > 0.3;
  }
}

void FrameEncoder::pen(std::string & out, const CellBuffer::Cell & cell) {
  Ink fg = ink(cell.fg, false), bg = ink(cell.bg, true);
  bool reverse = false, dim = false;
  if (profile_.colors == ColorDepth::NONE) monoStyle(cell, reverse, dim);
  std::string params;
  auto add = [&](const std::string & p) {
    if (!params.empty()) params += ';';
    params += p;
  };
  auto color = [&](const Ink & k, bool background) {
    if (k.kind == Ink::DEFAULT) return std::string(background ? "49" : "39");
    if (k.kind == Ink::RGB) {
      return std::string(background ? "48;2;" : "38;2;") + std::to_string(k.value >> 16) + ";" +
             std::to_string((k.value >> 8) & 0xFF) + ";" + std::to_string(k.value & 0xFF);
    }
    if (profile_.colors == ColorDepth::C8) return std::to_string((background ? 40 : 30) + k.value);
    if (profile_.colors == ColorDepth::C16) return std::to_string((k.value < 8 ? (background ? 40 : 30) + k.value : (background ? 100 : 90) + k.value - 8));
    return std::string(background ? "48;5;" : "38;5;") + std::to_string(k.value);
  };
  if (!pen_known_) {
    add("0");
    if (cell.bold()) add("1");
    if (dim) add("2");
    if (cell.italic) add("3");
    if (cell.underline) add("4");
    if (reverse) add("7");
    if (fg.kind != Ink::DEFAULT) add(color(fg, false));
    if (bg.kind != Ink::DEFAULT) add(color(bg, true));
  } else {
    // SGR 22 ends bold and dim together, so one that stays on is asked for again.
    if ((bold_ && !cell.bold()) || (dim_ && !dim)) {
      add("22");
      if (cell.bold()) add("1");
      if (dim) add("2");
    } else {
      if (cell.bold() && !bold_) add("1");
      if (dim && !dim_) add("2");
    }
    if (cell.italic != italic_) add(cell.italic ? "3" : "23");
    if (cell.underline != underline_) add(cell.underline ? "4" : "24");
    if (reverse != reverse_) add(reverse ? "7" : "27");
    if (fg != fg_) add(color(fg, false));
    if (bg != bg_) add(color(bg, true));
  }
  if (!params.empty()) out += "\x1b[" + params + "m";
  pen_known_ = true;
  fg_ = fg;
  bg_ = bg;
  bold_ = cell.bold();
  italic_ = cell.italic;
  underline_ = cell.underline;
  reverse_ = reverse;
  dim_ = dim;
}

std::string FrameEncoder::encode(const CellBuffer & frame, const CursorRequest & cursor, const std::string & images) {
  int rows = frame.rows(), cols = frame.cols();
  std::string body;
  if (full_ || !last_ || last_->rows() != rows || last_->cols() != cols) {
    // After a clear the screen is all blank cells in the default colours.
    body += "\x1b[0m\x1b[H\x1b[2J";
    pen_known_ = true;
    fg_ = bg_ = Ink{};
    bold_ = italic_ = underline_ = reverse_ = dim_ = false;
    cursor_y_ = cursor_x_ = 0;
    last_.emplace(rows, cols);
    full_ = false;
  }
  CellBuffer & last = *last_;
  double now = clock_();
  if (lit_since_.size() != static_cast<size_t>(rows) * static_cast<size_t>(cols)) lit_since_.assign(static_cast<size_t>(rows) * static_cast<size_t>(cols), -1.0);
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      const CellBuffer::Cell & raw = frame.at(r, c);
      size_t index = static_cast<size_t>(r) * static_cast<size_t>(cols) + static_cast<size_t>(c);
      if (raw.image || raw.continuation) lit_since_[index] = -1.0;
      // A lit cell is bold for its first kBoldHold; the cell the terminal is sent (and
      // compared with the next frame) is that one.
      CellBuffer::Cell lit_cell;
      const CellBuffer::Cell * shown = &raw;
      if (!raw.image && !raw.continuation && litBold(index, raw, now) && !raw.bold()) {
        lit_cell = raw;
        lit_cell.weight = 700;
        shown = &lit_cell;
      }
      const CellBuffer::Cell & cell = *shown;
      if (cell.image) {
        // The image draws this cell. What was under it is blanked as the
        // image arrives (in the same frame, before it), so a selection in
        // the terminal finds nothing there; after that nothing is sent.
        if (!last.at(r, c).image) {
          CellBuffer::Cell blank;
          moveTo(body, r, c);
          pen(body, blank);
          body += ' ';
          blank.image = cell.image;
          last.put(r, c, blank);
          cursor_x_ = c + 1 >= cols ? -1 : c + 1;
        }
        continue;
      }
      if (cell.continuation) continue;
      bool wide = c + 1 < cols && frame.at(r, c + 1).continuation;
      bool damaged = !sameCell(cell, last.at(r, c)) || (wide && !sameCell(frame.at(r, c + 1), last.at(r, c + 1)));
      if (!damaged) continue;
      moveTo(body, r, c);
      pen(body, cell);
      // A glyph in its own background colour is invisible: a space is the
      // same picture in fewer bytes, unless an underline would show.
      bool invisible = fg_ == bg_ && fg_.kind != Ink::DEFAULT && !cell.underline;
      if (invisible) body += wide ? "  " : " ";
      else body += cell.text.empty() ? std::string(" ") : cell.text;
      last.put(r, c, cell);
      int width = wide ? 2 : 1;
      if (wide) last.put(r, c + 1, frame.at(r, c + 1));
      cursor_x_ = c + width;
      // At the right edge the cursor stays put or waits to wrap; and after
      // a cluster the terminal may draw at another width, its column is
      // not known. Either way the next cell needs an explicit move.
      if (cursor_x_ >= cols || (!invisible && widthRisky(cell.text))) cursor_x_ = -1;
      if (wide) c++;
    }
  }

  bool cursor_changed = cursor.visible != cursor_visible_ ||
                        (cursor.visible && (cursor.y != shown_y_ || cursor.x != shown_x_));
  if (!images.empty()) {
    body += images;
    cursor_y_ = cursor_x_ = -1;
  }
  if (body.empty() && !cursor_changed) return "";
  std::string out;
  if (profile_.sync == SyncForm::MODE) out += "\x1b[?2026h";
  else if (profile_.sync == SyncForm::DCS) out += "\x1bP=1s\x1b\\";
  if (cursor_visible_ && !body.empty()) out += "\x1b[?25l";
  out += body;
  if (cursor.visible) {
    moveTo(out, cursor.y, cursor.x);
    if (!cursor_visible_ || !body.empty()) out += "\x1b[?12l\x1b[?25h";
    shown_y_ = cursor.y;
    shown_x_ = cursor.x;
  } else if (cursor_visible_) {
    out += "\x1b[?25l";
  }
  cursor_visible_ = cursor.visible;
  if (profile_.sync == SyncForm::MODE) out += "\x1b[?2026l";
  else if (profile_.sync == SyncForm::DCS) out += "\x1bP=2s\x1b\\";
  return out;
}

}  // namespace cellui::term
