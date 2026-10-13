#pragma once

#include <cellui/Input.h>
#include <cellui/StyleProvider.h>

#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <optional>

namespace cellui {

// A rectangle of character cells that a widget draws into: its own position
// and size under a parent plane, one current style (colours and attributes)
// that putstr() writes with, and the places a backend can offer text entry
// and a small list picker. A backend implements it for its screen; the
// headless BufferPlane implements it over a CellBuffer.
class UIPlane {
 public:
  // `styles` is the application's theme, which outlives every plane; a child
  // plane shares its parent's.
  UIPlane(std::shared_ptr<void> app_context, const StyleProvider & styles) : app_context_(std::move(app_context)), styles_(styles) { }
  virtual ~UIPlane() { }

  const StyleProvider & getStyles() const { return styles_; }

  virtual void resize(int rows, int cols) {
    setDim(std::pair(rows, cols));
  }
  virtual void move(int y, int x) {
    setPosition(std::pair(y, x));
  }
  // Raises this plane above every sibling under the same parent - default
  // no-op for a backend with no z-order of its own. Two sibling widgets
  // sharing one screen rectangle need this to say which is on top, since a
  // plane resized down to nothing is not reliably hidden everywhere.
  virtual void moveToTop() { }
  // Takes this plane (and its children) out of what gets drawn, or puts it
  // back (above its former siblings). Its contents and state are kept, so
  // a hidden plane can sit anywhere without ever showing, whatever the
  // screen size becomes. Default no-op.
  virtual void setVisible(bool) {}
  // Whether the display can draw the 2x3 sextant glyphs (Symbols for Legacy
  // Computing, U+1FB00+). The default is false, the safe answer for a plane
  // with no display to ask; a backend answers once from what it detected.
  // Every widget that paints sub-character-resolution content asks the plane
  // instead of guessing for itself.
  virtual bool canRenderSextants() const { return false; }
  // How much larger than at 96 dpi to draw anything sized in pixels
  // (icons, text, line widths; hairlines stay one pixel). A terminal
  // reports no DPI, so its backend guesses from the cell size.
  virtual double displayScale() const { return 1.0; }
  // The plane's size in pixels, when the display can show pixels here;
  // false when it cannot (no image protocol, or the cell size unknown), and
  // a chart then draws in cells.
  virtual bool pixelSize(int & height, int & width) const {
    (void)height;
    (void)width;
    return false;
  }
  // Shows `pixels` (width*height, row 0 at the top, bytes R, G, B, A) from
  // the plane's top-left corner in place of its text, until erase(). Only
  // where pixelSize() said yes.
  virtual void drawImage(const std::vector<uint32_t> & pixels, int width, int height) {
    (void)pixels;
    (void)width;
    (void)height;
  }
  virtual void setFgColor(int r, int g, int b) = 0;
  virtual void setBgColor(int r, int g, int b) = 0;
  virtual void setUnderline(bool b) = 0;
  virtual void setBold(bool b) = 0;
  virtual void setItalic(bool b) = 0;
  // What putstr() writes next is drawn over what lies below with its
  // background averaged with that one's (a translucent popup), and its
  // foreground leaning to white or black against the result. Default no-op.
  virtual void setBgBlend(bool) { }
  virtual void setFgContrast(bool) { }
  virtual void erase() = 0;
  virtual void putstr(int y, int x, const std::string & s) = 0;
  virtual std::unique_ptr<UIPlane> createChild() = 0;
  virtual void drawBorder() = 0;
  virtual bool offerInput(const Input & input) = 0;

  // A one-line text field. y/x/rows/cols position and size it in this
  // plane's own coordinates: x == -1 (the default) means right after the
  // prompt, and rows/cols == -1 mean the rest of the plane. initial_text
  // seeds the field (editing an existing name starts from what it says);
  // text_color is the typed text's colour, the theme's reader_text_color
  // unless a caller needs another against its own backdrop.
  virtual void showReader(const std::string & prompt = "", int y = 0, int x = -1, int rows = -1, int cols = -1,
			   const std::string & initial_text = "",
			   std::optional<Color> text_color = std::nullopt) = 0;
  // Closes the field and returns what was typed.
  virtual std::string closeReader() = 0;
  virtual bool readerActive() const = 0;
  // What has been typed so far, without ending the session - a prompt with
  // completion needs it on every Tab.
  virtual std::string getReaderContents() const = 0;
  // Replaces what has been typed, keeping the field open (a completed name).
  virtual void setReaderContents(const std::string & text) = 0;
  // Shows a short status ("[No match]") in a small floating plane of its own
  // at column `x` of this plane's row 0, above the field. It is a plane of
  // its own, not text drawn into the field's, because a backend's field may
  // read its contents back from what is displayed. Calling again with a new
  // `x` or text replaces what was shown. Only valid while readerActive().
  virtual void showReaderIndicator(int x, const std::string & s) = 0;
  // Hides it; a no-op if nothing is shown. Only valid while readerActive().
  virtual void hideReaderIndicator() = 0;

  // A small floating list picker raised above this plane, not text drawn
  // into its content. y/x/rows/cols position and size it in this plane's
  // coordinates. Items are added afterwards with addItem(); `item_count`
  // (how many will follow) is needed up front so the list is exactly that
  // tall instead of padded to `rows`. A second call while one is active is a
  // no-op: closePicker() first to replace it.
  virtual void showPicker(int y, int x, int rows, int cols, int item_count) = 0;
  // One row: `id` is the visible text and what getPickerSelection() reports
  // while this row is highlighted; `description` is optional extra text
  // shown beside it.
  virtual void addItem(const std::string & id, const std::string & description) = 0;
  // Highlights row `index` (0-based, in addItem() order). The picker starts
  // on row 0, so a caller that wants the current choice highlighted calls
  // this once after the last addItem(). Out of range, or with no picker, it
  // does nothing.
  virtual void selectPickerItem(int index) = 0;
  virtual bool pickerActive() const = 0;
  // The highlighted row's `id`, "" with no picker or no items. Read it
  // before closePicker(), which reports nothing: committing and cancelling
  // end the same way, and only the caller knows which happened.
  virtual std::string getPickerSelection() const = 0;
  virtual void closePicker() = 0;
  virtual void refresh() = 0;
  
  const std::pair<int, int> & getPosition() const { return plane_pos; }
  const std::pair<int, int> & getDim() const { return plane_dim; }

  // The application's own object (opaque here), handed down to child planes.
  const std::shared_ptr<void> & getAppContext() const { return app_context_; }

protected:
  void setDim(std::pair<int, int> dim) { plane_dim = dim; }
  void setPosition(std::pair<int, int> pos) { plane_pos = pos; }
  
private:
  std::shared_ptr<void> app_context_;
  const StyleProvider & styles_;
  std::pair<int, int> plane_dim, plane_pos;
};

}  // namespace cellui
