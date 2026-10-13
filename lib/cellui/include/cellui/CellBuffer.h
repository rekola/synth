#pragma once

#include <cellui/Backdrop.h>
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
    // When composed over another plane: the background is averaged with
    // what is below, and the foreground leans to white or black against the
    // final background.
    bool bg_blend = false;
    bool fg_contrast = false;
    // A cell an image covers: the placement's index + 1 (see ImagePlacement).
    int image = 0;
    // What lies below shows through blurred and dimmed; with no text or
    // colours of its own the cell is just that.
    Backdrop backdrop;
  };

  struct Style {
    std::optional<Color> fg, bg;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool bg_blend = false;
    bool fg_contrast = false;
    Backdrop backdrop;
  };

  CellBuffer(int rows, int cols);

  int rows() const { return rows_; }
  int cols() const { return cols_; }
  const Cell & at(int row, int col) const { return cells_[index(row, col)]; }
  // Replaces one cell (a double-width character is a lead cell followed by
  // its continuation cell; the caller sets both).
  void put(int row, int col, const Cell & cell) {
    if (row >= 0 && row < rows_ && col >= 0 && col < cols_) cells_[index(row, col)] = cell;
  }
  // A cell nothing was written to (or that was erased): transparent when a
  // plane is drawn over another.
  static bool isBlank(const Cell & c) {
    return c.text == " " && !c.fg && !c.bg && !c.bold && !c.italic && !c.underline && !c.continuation &&
           !c.bg_blend && !c.fg_contrast && !c.image && !c.backdrop.active();
  }

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
