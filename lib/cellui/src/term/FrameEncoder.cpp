#include <cellui/term/FrameEncoder.h>

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
  return !a || (a->getRed() == b->getRed() && a->getGreen() == b->getGreen() && a->getBlue() == b->getBlue());
}

bool sameCell(const CellBuffer::Cell & a, const CellBuffer::Cell & b) {
  return a.text == b.text && a.continuation == b.continuation && a.image == b.image && a.bold == b.bold && a.italic == b.italic &&
         a.underline == b.underline && sameColor(a.fg, b.fg) && sameColor(a.bg, b.bg);
}

}  // namespace

int nearest256(const Color & c) {
  int r = c.getRed(), g = c.getGreen(), b = c.getBlue();
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
  int r = c.getRed(), g = c.getGreen(), b = c.getBlue();
  if ((r >> 3) == (g >> 3) && (g >> 3) == (b >> 3)) return r < 64 ? 0 : 7;
  return (r >= 128 ? 1 : 0) | (g >= 128 ? 2 : 0) | (b >= 128 ? 4 : 0);
}

EncoderProfile EncoderProfile::from(const Capabilities & caps) {
  EncoderProfile p;
  p.colors = caps.colors;
  p.column_move = caps.column_move;
  p.sync = caps.sync;
  if (caps.distinct_default_bg) p.distinct_from = caps.default_bg;
  return p;
}

FrameEncoder::FrameEncoder(EncoderProfile profile) : profile_(std::move(profile)), full_(true) { }

FrameEncoder::Ink FrameEncoder::ink(const std::optional<Color> & c, bool background) const {
  Ink k;
  if (!c) return k;
  switch (profile_.colors) {
    case ColorDepth::NONE:
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
  k.value = (c->getRed() << 16) | (c->getGreen() << 8) | c->getBlue();
  if (background && profile_.distinct_from) {
    const Color & d = *profile_.distinct_from;
    if (k.value == ((d.getRed() << 16) | (d.getGreen() << 8) | d.getBlue())) k.value ^= 1;
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

void FrameEncoder::pen(std::string & out, const CellBuffer::Cell & cell) {
  Ink fg = ink(cell.fg, false), bg = ink(cell.bg, true);
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
    return std::string(background ? "48;5;" : "38;5;") + std::to_string(k.value);
  };
  if (!pen_known_) {
    add("0");
    if (cell.bold) add("1");
    if (cell.italic) add("3");
    if (cell.underline) add("4");
    if (fg.kind != Ink::DEFAULT) add(color(fg, false));
    if (bg.kind != Ink::DEFAULT) add(color(bg, true));
  } else {
    if (cell.bold != bold_) add(cell.bold ? "1" : "22");
    if (cell.italic != italic_) add(cell.italic ? "3" : "23");
    if (cell.underline != underline_) add(cell.underline ? "4" : "24");
    if (fg != fg_) add(color(fg, false));
    if (bg != bg_) add(color(bg, true));
  }
  if (!params.empty()) out += "\x1b[" + params + "m";
  pen_known_ = true;
  fg_ = fg;
  bg_ = bg;
  bold_ = cell.bold;
  italic_ = cell.italic;
  underline_ = cell.underline;
}

std::string FrameEncoder::encode(const CellBuffer & frame, const CursorRequest & cursor, const std::string & images) {
  int rows = frame.rows(), cols = frame.cols();
  std::string body;
  if (full_ || !last_ || last_->rows() != rows || last_->cols() != cols) {
    // After a clear the screen is all blank cells in the default colours.
    body += "\x1b[0m\x1b[H\x1b[2J";
    pen_known_ = true;
    fg_ = bg_ = Ink{};
    bold_ = italic_ = underline_ = false;
    cursor_y_ = cursor_x_ = 0;
    last_.emplace(rows, cols);
    full_ = false;
  }
  CellBuffer & last = *last_;
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      const CellBuffer::Cell & cell = frame.at(r, c);
      if (cell.image) {
        // The image draws this cell; the text under it is not sent.
        if (!last.at(r, c).image) last.put(r, c, cell);
        continue;
      }
      if (cell.continuation) continue;
      bool wide = c + 1 < cols && frame.at(r, c + 1).continuation;
      bool damaged = !sameCell(cell, last.at(r, c)) || (wide && !sameCell(frame.at(r, c + 1), last.at(r, c + 1)));
      if (!damaged) continue;
      moveTo(body, r, c);
      pen(body, cell);
      body += cell.text.empty() ? std::string(" ") : cell.text;
      last.put(r, c, cell);
      int width = wide ? 2 : 1;
      if (wide) last.put(r, c + 1, frame.at(r, c + 1));
      cursor_x_ = c + width;
      // At the right edge the cursor stays put or waits to wrap; and after
      // a cluster the terminal may draw at another width, its column is
      // not known. Either way the next cell needs an explicit move.
      if (cursor_x_ >= cols || widthRisky(cell.text)) cursor_x_ = -1;
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
