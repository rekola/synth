#pragma once

#include <cellui/Backdrop.h>
#include <cellui/BackdropLayer.h>
#include <cellui/Input.h>
#include <cellui/KeymapProfile.h>
#include <cellui/MouseCursor.h>
#include <cellui/StyleProvider.h>

#include <climits>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <optional>

namespace cellui {

class Widget;

// A rectangle of character cells that a widget draws into: its own position
// and size under a parent plane, one current style (colours and attributes)
// that putstr() writes with, and the places a backend can offer text entry
// and a small list picker. A backend implements it for its screen; the
// headless BufferPlane implements it over a CellBuffer.
class Plane {
 public:
  // `styles` is the application's theme, which outlives every plane; a child
  // plane shares its parent's.
  Plane(std::shared_ptr<void> app_context, const StyleProvider & styles) : app_context_(std::move(app_context)), styles_(styles) { }
  virtual ~Plane() { }

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
  // How far above its parent the plane floats, in cell widths
  // (Theme::elevation's steps), so
  // a child adds to its parent's height; negative sinks it. What is higher
  // is drawn over what is lower wherever it sits in the tree, and planes of
  // one height in tree order. Default no-op for a display with no layers.
  virtual void setElevation(float) {}
  virtual float elevation() const { return 0; }
  // The pointer's shape over the plane's rectangle. A plane that leaves it
  // at INHERIT shows what is below it, so a popup sets DEFAULT to say its
  // area is not what lies under it. Default no-op for a display with no
  // pointer.
  virtual void setMouseCursor(MouseCursor) {}
  virtual MouseCursor mouseCursor() const { return MouseCursor::INHERIT; }
  // A name for the plane in a test's or an inspector's view of the tree
  // (PlaneInfo); default no-op.
  virtual void setName(const std::string &) {}
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
  // The family of key chords this display uses (widgets bind per profile).
  virtual KeymapProfile keymapProfile() const { return KeymapProfile::EMACS; }
  // Whether setScrollOffset() shifts a plane here.
  virtual bool smoothScroll() const { return false; }
  // The plane's size in pixels, when the display can show pixels here;
  // false when it cannot (no image protocol, or the cell size unknown), and
  // a chart then draws in cells.
  virtual bool pixelSize(int & height, int & width) const {
    (void)height;
    (void)width;
    return false;
  }
  // The size of one cell in pixels, under the same condition.
  virtual bool cellPixelSize(int & height, int & width) const {
    (void)height;
    (void)width;
    return false;
  }
  // Shows `pixels` (width*height, row 0 at the top, bytes R, G, B, A) from
  // the plane's top-left corner in place of its text, until erase(). Only
  // where pixelSize() said yes.
  virtual void drawImage(const std::vector<uint32_t> & pixels, int width, int height, uint64_t id = 0) {
    drawImageAt(0, 0, pixels, width, height, id);
  }
  // The same from cell (row, col), covering only the cells the image
  // reaches; the plane's text shows in the others. A plane holds one image
  // per position, each replacing the last drawn there. `id` names the
  // picture: the widget's own id for it, or 0 for a hash of its pixels. The
  // same picture drawn at another position (after erase()) is the same
  // image, moved: a backend places it again and sends no pixels. With an id,
  // new pixels under the same id are the same image changed. The image may
  // reach beyond the plane (row or col negative, or past its edge); only the
  // part inside the plane shows, and inside plane rows clip_top up to
  // clip_bottom, so a picture partly scrolled out of view is still the whole
  // picture.
  virtual void drawImageAt(int row, int col, const std::vector<uint32_t> & pixels, int width, int height, uint64_t id = 0,
                           int clip_top = INT_MIN, int clip_bottom = INT_MAX) {
    (void)clip_top;
    (void)clip_bottom;
    (void)id;
    (void)row;
    (void)col;
    (void)pixels;
    (void)width;
    (void)height;
  }
  // The colours of what is drawn from now on. A colour may be wide (outside
  // sRGB, or above SDR white); a display that cannot show that takes
  // Color::toSrgb8().
  virtual void setFgColor(const Color & color) = 0;
  virtual void setBgColor(const Color & color) = 0;
  // 8-bit sRGB.
  void setFgColor(int r, int g, int b) { setFgColor(Color(r, g, b)); }
  void setBgColor(int r, int g, int b) { setBgColor(Color(r, g, b)); }
  // The terminal's own background for what is drawn from now on, and for
  // what the plane shows where it draws nothing: a row the display cannot
  // always draw to its end (the last cell of the last row) must not differ
  // from the rest of it. Default no-ops for a plane with no display default.
  virtual void setBgDefault() { }
  virtual void makeTransparent() { }
  virtual void setUnderline(bool b) = 0;
  virtual void setBold(bool b) = 0;
  virtual void setItalic(bool b) = 0;
  // What putstr() writes next is drawn over what lies below with its
  // background averaged with that one's (a translucent popup), and its
  // foreground leaning to white or black against the result. Default no-op.
  virtual void setBgBlend(bool) { }
  virtual void setFgContrast(bool) { }
  // What putstr() writes next shows the planes below it through `backdrop`
  // (blurred and dimmed); a space written with no colours is nothing but
  // that. Default no-op: a display that can't, shows them as they are.
  virtual void setBackdrop(const Backdrop &) { }
  // How far the cells drawn next stand above the plane, in cell widths, like
  // a card on it (0: flat). A run of them in a row is one piece; with
  // `joins_above` it is one with the run of the same height and columns
  // right above it. A display that shows height (shadows) shows them raised;
  // elsewhere nothing changes. Default no-op.
  virtual void setLift(float, bool joins_above = false) { (void)joins_above; }
  // Shows what the plane draws shifted up by `rows` and left by `cols`, each
  // a fraction of a cell (0 up to, not including, 1), clipped to the plane,
  // where DisplayInfo::smooth_scroll says the display can: a view scrolled by
  // less than a cell. The plane then holds one row and one column more than
  // it shows, for the content to shift into view. Images in such a plane
  // are not shifted. Default no-op.
  virtual void setScrollOffset(double rows, double cols) { (void)rows; (void)cols; }
  // A translucent band over the plane (see Band), where the display scrolls
  // smoothly; it stays until cleared. Default no-op.
  virtual void setBand(const Band & band) { (void)band; }
  virtual void clearBand() { }
  // How the plane combines with the planes below it: instead of replacing
  // their cells, the colours its cells set (backgrounds; its text is not drawn)
  // are laid over what is there with `mode`, `alpha` of the way, text below
  // included. Any plane can; the display draws it blended or the cells are
  // blended where it composes them. Default no-op.
  virtual void setBlend(BlendMode mode, float alpha = 1.0f) { (void)mode; (void)alpha; }
  virtual void erase() = 0;
  virtual void putstr(int y, int x, const std::string & s) = 0;
  virtual std::unique_ptr<Plane> createChild() = 0;
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

  // The widget that draws on this plane, or null for a plane no widget owns
  // (a root, a scrim). Set by the widget; used to find the widget under a
  // point from the plane that covers it.
  Widget * owner() const { return owner_; }
  void setOwner(Widget * widget) { owner_ = widget; }

protected:
  void setDim(std::pair<int, int> dim) { plane_dim = dim; }
  void setPosition(std::pair<int, int> pos) { plane_pos = pos; }
  
private:
  std::shared_ptr<void> app_context_;
  Widget * owner_ = nullptr;
  const StyleProvider & styles_;
  std::pair<int, int> plane_dim, plane_pos;
};

}  // namespace cellui
