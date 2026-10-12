#pragma once

#include <cellui/CellBuffer.h>
#include <cellui/LineEditor.h>
#include <cellui/UIPlane.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cellui {

// A plane that keeps its own cells in memory instead of drawing on a screen,
// the way a screen's planes do: drawing, moving, resizing, hiding and raising
// a plane work on the plane, and refresh() on any plane of the tree composes
// them into the root's CellBuffer - children over their parent in the order
// they were created or raised, a cell nothing was written to showing what is
// below it, every child clipped to its parent. A translucent cell
// (setBgBlend()) averages its background with what is below.
//
// It is what the terminal backend draws: the terminal shows the composed
// buffer. So it also has the reader (a one-line field over its own child
// plane, edited with LineEditor's keys, its cursor reported by
// cursorPosition()), the reader's indicator, and the picker (a boxed list
// in a child plane, moved with the arrows, PgUp/PgDn, the wheel and a
// click). Widgets can be laid out, drawn and read back in a test, or
// printed by the gallery.
class BufferPlane : public UIPlane {
 public:
  // The root plane covers the whole buffer.
  BufferPlane(CellBuffer & target, const StyleProvider & styles, std::shared_ptr<void> app_context = nullptr);
  ~BufferPlane() override;
  BufferPlane(const BufferPlane &) = delete;
  BufferPlane & operator=(const BufferPlane &) = delete;

  void resize(int rows, int cols) override;
  void moveToTop() override;
  void setVisible(bool visible) override { visible_ = visible; }
  bool isVisible() const { return visible_; }

  void setFgColor(int r, int g, int b) override { style_.fg = Color(r, g, b); }
  void setBgColor(int r, int g, int b) override { style_.bg = Color(r, g, b); }
  void setUnderline(bool b) override { style_.underline = b; }
  void setBold(bool b) override { style_.bold = b; }
  void setItalic(bool b) override { style_.italic = b; }
  void setBgBlend(bool b) override { style_.bg_blend = b; }
  void setFgContrast(bool b) override { style_.fg_contrast = b; }
  void erase() override { local_.clear(0, 0, local_.rows(), local_.cols()); }
  void putstr(int y, int x, const std::string & s) override;
  std::unique_ptr<UIPlane> createChild() override;
  void drawBorder() override;
  // Feeds the reader or the picker, whichever is open.
  bool offerInput(const Input & input) override;
  // Composes the whole tree into the root's buffer.
  void refresh() override;

  void showReader(const std::string & prompt, int y, int x, int rows, int cols, const std::string & initial_text,
                  std::optional<Color> text_color) override;
  std::string closeReader() override;
  bool readerActive() const override { return editor_ != nullptr; }
  std::string getReaderContents() const override { return editor_ ? editor_->text() : ""; }
  void setReaderContents(const std::string & text) override;
  // Inserts text at the reader's cursor (a paste).
  void insertReaderText(const std::string & text);
  void showReaderIndicator(int x, const std::string & s) override;
  void hideReaderIndicator() override { indicator_.reset(); }

  void showPicker(int y, int x, int rows, int cols, int item_count) override;
  void addItem(const std::string & id, const std::string & description) override;
  void selectPickerItem(int index) override;
  bool pickerActive() const override { return picker_ != nullptr; }
  std::string getPickerSelection() const override;
  void closePicker() override;

  // The colours a translucent cell over nothing blends with: the terminal's
  // own (root only).
  void setDefaultColors(Color fg, Color bg) {
    default_fg_ = fg;
    default_bg_ = bg;
  }
  // Where the terminal's cursor goes: in the open reader, if any (root only).
  std::optional<std::pair<int, int>> cursorPosition() const;
  // This plane's top-left corner on the screen.
  std::pair<int, int> screenPosition() const;
  // The cells as written, before composition.
  const CellBuffer & cells() const { return local_; }

 private:
  struct Picker;

  explicit BufferPlane(BufferPlane & parent);
  std::unique_ptr<BufferPlane> makeChild() { return std::unique_ptr<BufferPlane>(new BufferPlane(*this)); }

  BufferPlane & root() { return parent_ ? parent_->root() : *this; }
  const BufferPlane & root() const { return parent_ ? parent_->root() : *this; }
  // Draws this plane and its children onto `out` with the plane's top-left
  // corner at (row, col), inside the clip rectangle.
  void compose(CellBuffer & out, int row, int col, int clip_row, int clip_col, int clip_rows, int clip_cols) const;
  bool shown() const;
  void drawReader();
  void drawPicker();

  CellBuffer local_;
  CellBuffer * target_ = nullptr;  // the root's output
  BufferPlane * parent_ = nullptr;
  std::vector<BufferPlane *> children_;  // bottom to top
  CellBuffer::Style style_;
  bool visible_ = true;

  // The reader: the text, the plane it shows in, its colour and column.
  std::unique_ptr<LineEditor> editor_;
  std::unique_ptr<BufferPlane> reader_plane_;
  Color reader_color_;
  int reader_x_ = 0;
  int reader_cursor_ = 0;
  std::unique_ptr<BufferPlane> indicator_;
  std::unique_ptr<Picker> picker_;

  // Root only.
  BufferPlane * active_reader_ = nullptr;
  Color default_fg_ = Color(255, 255, 255), default_bg_ = Color(0, 0, 0);
};

}  // namespace cellui
