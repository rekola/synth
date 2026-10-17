#include <cellui/CellBuffer.h>

#include <cellui/Utf8.h>

#include <algorithm>

namespace cellui {

namespace {

// Decodes one codepoint at s[i], advancing i; malformed bytes become U+FFFD.
uint32_t decode(const std::string & s, size_t & i) {
  unsigned char c = static_cast<unsigned char>(s[i]);
  int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
  if (extra < 0 || i + static_cast<size_t>(extra) >= s.size()) {
    i++;
    return 0xFFFD;
  }
  uint32_t cp = extra == 0 ? c : c & (0x3Fu >> extra);
  for (int k = 1; k <= extra; k++) {
    unsigned char d = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
    if ((d & 0xC0) != 0x80) {
      i++;
      return 0xFFFD;
    }
    cp = (cp << 6) | (d & 0x3Fu);
  }
  i += static_cast<size_t>(extra) + 1;
  return cp;
}

}  // namespace

CellBuffer::CellBuffer(int rows, int cols)
  : rows_(std::max(0, rows)), cols_(std::max(0, cols)),
    cells_(static_cast<size_t>(rows_) * static_cast<size_t>(cols_)) { }

void CellBuffer::write(int row, int col, const std::string & text, const Style & style,
                       int clip_row, int clip_col, int clip_rows, int clip_cols) {
  int row_lo = std::max(clip_row, 0), row_hi = std::min(clip_row + clip_rows, rows_);
  int col_lo = std::max(clip_col, 0), col_hi = std::min(clip_col + clip_cols, cols_);
  if (row < row_lo || row >= row_hi) return;
  int last_col = -1;  // the cell the last glyph started in, for combining marks
  for (size_t i = 0; i < text.size();) {
    uint32_t cp = decode(text, i);
    if (cp < 0x20 || cp == 0x7F) continue;
    std::string glyph = utf8::encodeCodepoint(cp);
    int width = utf8::displayWidth(glyph);
    if (width == 0) {
      if (last_col >= col_lo && last_col < col_hi) cells_[index(row, last_col)].text += glyph;
      continue;
    }
    if (col >= col_lo && col + width <= col_hi) {
      Cell & cell = cells_[index(row, col)];
      cell = Cell{glyph, style.fg, style.bg, style.bold, style.italic, style.underline, false, style.bg_blend, style.fg_contrast, 0, style.backdrop, style.lift, style.lift_joined, style.lift_shadow};
      if (width == 2) {
        cells_[index(row, col + 1)] = Cell{"", style.fg, style.bg, style.bold, style.italic, style.underline, true, style.bg_blend, style.fg_contrast, 0, style.backdrop, style.lift, style.lift_joined, style.lift_shadow};
      }
      last_col = col;
    } else {
      last_col = -1;
    }
    col += width;
  }
}

void CellBuffer::clear(int row, int col, int rows, int cols) {
  for (int r = std::max(row, 0); r < std::min(row + rows, rows_); r++) {
    for (int c = std::max(col, 0); c < std::min(col + cols, cols_); c++) cells_[index(r, c)] = Cell{};
  }
}

std::string CellBuffer::toPlainText() const {
  std::string out;
  for (int r = 0; r < rows_; r++) {
    std::string line;
    for (int c = 0; c < cols_; c++) {
      const Cell & cell = at(r, c);
      if (!cell.continuation) line += cell.text;
    }
    while (!line.empty() && line.back() == ' ') line.pop_back();
    out += line;
    out += '\n';
  }
  while (out.size() > 1 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
  if (out == "\n") out.clear();
  return out;
}

std::string CellBuffer::toAnsi(int max_rows) const {
  std::string out;
  for (int r = 0; r < rows_ && (max_rows < 0 || r < max_rows); r++) {
    Style current;
    bool styled = false;
    for (int c = 0; c < cols_; c++) {
      const Cell & cell = at(r, c);
      if (cell.continuation) continue;
      Style want{cell.fg, cell.bg, cell.bold, cell.italic, cell.underline};
      bool same = styled ? (want.bold == current.bold && want.italic == current.italic &&
                            want.underline == current.underline &&
                            want.fg.has_value() == current.fg.has_value() &&
                            want.bg.has_value() == current.bg.has_value() &&
                            (!want.fg || (want.fg->getRed() == current.fg->getRed() &&
                                          want.fg->getGreen() == current.fg->getGreen() &&
                                          want.fg->getBlue() == current.fg->getBlue())) &&
                            (!want.bg || (want.bg->getRed() == current.bg->getRed() &&
                                          want.bg->getGreen() == current.bg->getGreen() &&
                                          want.bg->getBlue() == current.bg->getBlue())))
                         : !want.fg && !want.bg && !want.bold && !want.italic && !want.underline;
      if (!same) {
        out += "\x1b[0";
        if (want.bold) out += ";1";
        if (want.italic) out += ";3";
        if (want.underline) out += ";4";
        if (want.fg) out += ";38;2;" + std::to_string(want.fg->getRed()) + ";" + std::to_string(want.fg->getGreen()) + ";" + std::to_string(want.fg->getBlue());
        if (want.bg) out += ";48;2;" + std::to_string(want.bg->getRed()) + ";" + std::to_string(want.bg->getGreen()) + ";" + std::to_string(want.bg->getBlue());
        out += "m";
        current = want;
        styled = true;
      }
      out += cell.text;
    }
    out += "\x1b[0m\n";
  }
  return out;
}

}  // namespace cellui
