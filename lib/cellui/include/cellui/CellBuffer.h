#pragma once

#include <cellui/Color.h>

#include <optional>
#include <string>
#include <vector>

namespace cellui {

// A grid of character cells, each one grapheme cluster with its colours and
// text style. It is what a plane draws into when nothing is attached to a
// screen, and it renders itself as plain text (for tests) or as ANSI escape
// sequences (for a terminal).
class CellBuffer {
 public:
  struct Cell {
    std::string text = " ";
    std::optional<Color> fg, bg;  // unset = the terminal's own colour
    bool bold = false;
    bool italic = false;
    bool underline = false;
    // The right half of a double-width cell before it; draws nothing.
    bool continuation = false;
  };

  struct Style {
    std::optional<Color> fg, bg;
    bool bold = false;
    bool italic = false;
    bool underline = false;
  };

  CellBuffer(int rows, int cols);

  int rows() const { return rows_; }
  int cols() const { return cols_; }
  const Cell & at(int row, int col) const { return cells_[index(row, col)]; }

  // Writes `text` from (row, col) in `style`, clipped to the rectangle
  // [clip_row, clip_row + clip_rows) x [clip_col, clip_col + clip_cols) and
  // to the buffer. A combining mark joins the cell before it, a double-width
  // character takes two cells, and one that does not fit in the clip is left
  // out. Control characters are ignored.
  void write(int row, int col, const std::string & text, const Style & style,
             int clip_row, int clip_col, int clip_rows, int clip_cols);
  void write(int row, int col, const std::string & text, const Style & style) {
    write(row, col, text, style, 0, 0, rows_, cols_);
  }

  // Resets a rectangle to blank cells in the terminal's own colours.
  void clear(int row, int col, int rows, int cols);

  // The text of the grid, one line per row, trailing blanks and trailing
  // empty lines removed.
  std::string toPlainText() const;

  // The grid as ANSI escape sequences: 24-bit colour, one line per row, the
  // style reset at the end of every line.
  std::string toAnsi(int max_rows = -1) const;

 private:
  size_t index(int row, int col) const { return static_cast<size_t>(row) * static_cast<size_t>(cols_) + static_cast<size_t>(col); }

  int rows_, cols_;
  std::vector<Cell> cells_;
};

}  // namespace cellui
