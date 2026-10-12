#pragma once

#include <cellui/CellBuffer.h>
#include <cellui/UIPlane.h>

#include <memory>
#include <string>
#include <vector>

namespace cellui {

// A plane that keeps its own cells in memory instead of drawing on a screen,
// the way a screen's planes do: drawing, moving, resizing, hiding and raising
// a plane work on the plane, and refresh() on any plane of the tree composes
// them into the root's CellBuffer - children over their parent in the order
// they were created or raised, a cell nothing was written to showing what is
// below it, every child clipped to its parent. Widgets can be laid out, drawn
// and read back in a test, or printed by the gallery. Input, the reader and
// the picker are not drawn by this plane: offerInput() takes nothing and the
// reader and picker stay inactive.
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

  void setFgColor(int r, int g, int b) override { style_.fg = Color(r, g, b); }
  void setBgColor(int r, int g, int b) override { style_.bg = Color(r, g, b); }
  void setUnderline(bool b) override { style_.underline = b; }
  void setBold(bool b) override { style_.bold = b; }
  void setItalic(bool b) override { style_.italic = b; }
  void erase() override { local_.clear(0, 0, local_.rows(), local_.cols()); }
  void putstr(int y, int x, const std::string & s) override;
  std::unique_ptr<UIPlane> createChild() override;
  void drawBorder() override;
  bool offerInput(const Input &) override { return false; }
  // Composes the whole tree into the root's buffer.
  void refresh() override;

  void showReader(const std::string &, int, int, int, int, const std::string &, std::optional<Color>) override { }
  std::string closeReader() override { return ""; }
  bool readerActive() const override { return false; }
  std::string getReaderContents() const override { return ""; }
  void setReaderContents(const std::string &) override { }
  void showReaderIndicator(int, const std::string &) override { }
  void hideReaderIndicator() override { }
  void showPicker(int, int, int, int, int) override { }
  void addItem(const std::string &, const std::string &) override { }
  void selectPickerItem(int) override { }
  bool pickerActive() const override { return false; }
  std::string getPickerSelection() const override { return ""; }
  void closePicker() override { }

 private:
  explicit BufferPlane(BufferPlane & parent);

  BufferPlane & root() { return parent_ ? parent_->root() : *this; }
  // Draws this plane and its children onto `out` with the plane's top-left
  // corner at (row, col), inside the clip rectangle.
  void compose(CellBuffer & out, int row, int col, int clip_row, int clip_col, int clip_rows, int clip_cols) const;

  CellBuffer local_;
  CellBuffer * target_ = nullptr;  // the root's output
  BufferPlane * parent_ = nullptr;
  std::vector<BufferPlane *> children_;  // bottom to top
  CellBuffer::Style style_;
  bool visible_ = true;
};

}  // namespace cellui
