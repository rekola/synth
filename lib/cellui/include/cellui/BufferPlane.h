#pragma once

#include <cellui/CellBuffer.h>
#include <cellui/UIPlane.h>

#include <memory>
#include <string>

namespace cellui {

// A plane that draws into a CellBuffer instead of a screen. A child plane is
// a rectangle of the same buffer, positioned relative to its parent and
// clipped to itself, so widgets can be laid out, drawn and read back in a
// test, or printed by the gallery. Input, the reader and the picker are not
// drawn by this plane: offerInput() takes nothing and the reader and picker
// stay inactive.
class BufferPlane : public UIPlane {
 public:
  // The root plane covers the whole buffer.
  BufferPlane(CellBuffer & buffer, const StyleProvider & styles, std::shared_ptr<void> app_context = nullptr);

  void setFgColor(int r, int g, int b) override { style_.fg = Color(r, g, b); }
  void setBgColor(int r, int g, int b) override { style_.bg = Color(r, g, b); }
  void setUnderline(bool b) override { style_.underline = b; }
  void setBold(bool b) override { style_.bold = b; }
  void setItalic(bool b) override { style_.italic = b; }
  void erase() override;
  void putstr(int y, int x, const std::string & s) override;
  std::unique_ptr<UIPlane> createChild() override;
  void drawBorder() override;
  bool offerInput(const Input &) override { return false; }
  void setVisible(bool visible) override { visible_ = visible; }
  void refresh() override { }

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
  BufferPlane(const BufferPlane & parent, int rows, int cols);

  // Where this plane's top-left cell is in the buffer.
  int originRow() const;
  int originCol() const;
  bool shown() const;

  CellBuffer & buffer_;
  const BufferPlane * parent_ = nullptr;
  CellBuffer::Style style_;
  bool visible_ = true;
};

}  // namespace cellui
