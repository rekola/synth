#ifndef _UIPLANE_H_
#define _UIPLANE_H_

#include <string>
#include <utility>
#include <memory>

class Controller;

class UIPlane {
 public:
  UIPlane(const std::shared_ptr<Controller> & _controller) : controller(_controller) { }
  virtual ~UIPlane() { }

  virtual void resize(int rows, int cols) {
    setDim(std::pair(rows, cols));
  }
  virtual void move(int y, int x) {
    setPosition(std::pair(y, x));
  }
  // Raises this plane above every sibling under the same parent - default
  // no-op (most UIPlane implementations have no z-order of their own to
  // manipulate). See UI.h's own session_view_/pattern_editor_ comment for
  // why this exists: two sibling widgets sharing one exact screen rect
  // need an explicit way to say which one is actually on top, since
  // resize()ing the other down to nothing isn't reliable (notcurses
  // itself refuses/ignores a plane resize to zero rows or columns,
  // leaving its last real content and z-position untouched - confirmed
  // via a pty+notcurses reproduction).
  virtual void moveToTop() { }
  // Whether this plane's own terminal reports Unicode 13 sextant glyph
  // support (Symbols for Legacy Computing, U+1FB00+) - default false, the
  // universally-safe answer for any UIPlane implementation with no real
  // terminal (or no capability of its own) to check. TerminalPlane's own
  // override figures this out once, from real notcurses_cansextant(), at
  // construction. Shared by every widget that paints sub-character-
  // resolution content (TerminalHeatmapChart's 2D field, PatternEditor's
  // own waveform boxes) so the same real check backs every one of them,
  // not a separately-guessed answer each.
  virtual bool canRenderSextants() const { return false; }
  virtual void setFgColor(int r, int g, int b) = 0;
  virtual void setBgColor(int r, int g, int b) = 0;
  virtual void setUnderline(bool b) = 0;
  virtual void setBold(bool b) = 0;
  virtual void setItalic(bool b) = 0;
  virtual void erase() = 0;
  virtual void putstr(int y, int x, const std::string & s) = 0;
  virtual std::unique_ptr<UIPlane> createChild() = 0;
  virtual void drawBorder() = 0;
  virtual bool offerInput(const InputEvent & input) = 0;
  // y/x/rows/cols position and size the reader plane explicitly, for a
  // caller (PatternEditor's annotation editing) that isn't a one-line
  // plane like StatusLine - x == -1 (default) means "right after the
  // prompt" and rows/cols == -1 means "the rest of the plane", both
  // matching the historical no-args behavior every existing call site
  // still relies on. initial_text seeds the reader's own content (e.g.
  // editing an existing annotation starts from what it already says, not
  // blank) rather than requiring a second call after this one. text_r/g/b
  // is the reader's own typed-glyph color - pink by default (matching
  // every other UI plane's own base cell fg, see TerminalUI::showReader()'s
  // own comment), overridable for a caller whose reader sits on a
  // background pink would read poorly against (PatternEditor's track-name
  // editor, over its own darkened per-track backdrop).
  virtual void showReader(const std::string & prompt = "", int y = 0, int x = -1, int rows = -1, int cols = -1,
			   const std::string & initial_text = "",
			   int text_r = 0xc0, int text_g = 0x80, int text_b = 0xc0) = 0;
  virtual std::string closeReader() = 0;
  virtual bool readerActive() const = 0;
  // Non-destructive read of the reader's current contents - unlike
  // closeReader(), doesn't end the reader session. StatusLine's M-x
  // autocomplete needs to inspect what's been typed so far without
  // closing the minibuffer on every Tab press.
  virtual std::string getReaderContents() const = 0;
  // Replaces the reader's contents with `text`, keeping it open - the
  // autocomplete counterpart to showReader()'s own initial_text seeding
  // above, used to fill in a completed command name.
  virtual void setReaderContents(const std::string & text) = 0;
  // Shows StatusLine's completion-status indicator ("[No match]"/"[Sole
  // completion]") in a small floating plane of its own, positioned at
  // column `x` on this plane's own row 0 (right after whatever's
  // currently typed) and raised above the reader. A dedicated plane, not
  // drawn directly onto the reader's own - confirmed empirically that even
  // blank/erasing writes onto the reader's own plane corrupt what
  // getReaderContents() itself reports back as typed (ncreader appears to
  // derive its own contents from what the plane actually displays, not a
  // fully independent buffer, so every such write permanently inflates
  // the length it reports, compounding on every redraw). Repositioning an
  // already-shown indicator (calling this again with a new `x`) is safe
  // and expected - each call fully replaces whatever the plane last
  // showed. Only valid while readerActive().
  virtual void showReaderIndicator(int x, const std::string & s) = 0;
  // Hides whatever showReaderIndicator() last drew - a no-op if nothing is
  // currently shown. Only valid while readerActive().
  virtual void hideReaderIndicator() = 0;
  // A small floating list-picker (ncselector-backed - a real overlay
  // plane raised above this one, not text drawn inline into this plane's
  // own content) - see OutlineView's own target-track picker for the
  // first real caller. y/x/rows/cols position and size it explicitly, in
  // this plane's own coordinate space (matching showReader()'s identical
  // convention). Items are added afterward via addItem(), not passed in
  // here - mirrors ncselector's own create-then-additem() shape, but
  // `item_count` (how many addItem() calls will follow) is still needed
  // up front to pin ncselector's own maxdisplay to exactly that many
  // rows - otherwise (maxdisplay 0, "use all available space") it
  // stretches/pads its body to fill whatever's left of a `rows` taller
  // than the item count actually needs, showing as blank rows above the
  // first item and below the last rather than a tightly-wrapped list. A
  // second call while already active is a no-op - closePicker() first to
  // replace it.
  virtual void showPicker(int y, int x, int rows, int cols, int item_count) = 0;
  // One row - `id` becomes both the visible primary text and what
  // getPickerSelection() reports back once this row is the highlighted
  // one (ncselector_item's own .option field); `description` is optional
  // supplementary text shown alongside it (.desc) - empty for a plain,
  // single-column-looking list.
  virtual void addItem(const std::string & id, const std::string & description) = 0;
  // Moves the highlighted row to `index` (0-based, in addItem() call
  // order) - always starts on row 0 right after showPicker() (ncselector's
  // own defidx can't be set to anything else up front through the create-
  // then-additem() shape this uses: ncselector_create() rejects a nonzero
  // defidx against the 0 static items it's given, since items are only
  // ever added afterward, one at a time, via addItem()), so a caller that
  // wants some other row highlighted when it opens - "highlight whatever
  // is already the current choice" - calls this once, after every addItem()
  // call, before the picker is ever shown to the user. Out-of-range or
  // called on an inactive picker is a harmless no-op.
  virtual void selectPickerItem(int index) = 0;
  virtual bool pickerActive() const = 0;
  // The currently-highlighted row's own `id` (addItem()'s own first
  // argument) - "" while inactive or with no items added yet. Read this
  // before closePicker() to know what the user picked - closePicker()
  // itself reports nothing back, since both committing and canceling end
  // the same way (destroying the plane) and only the caller knows which
  // one just happened.
  virtual std::string getPickerSelection() const = 0;
  virtual void closePicker() = 0;
  virtual void refresh() = 0;
  
  const std::pair<int, int> & getPosition() const { return plane_pos; }
  const std::pair<int, int> & getDim() const { return plane_dim; }

  std::shared_ptr<Controller> & getController() { return controller; }

protected:
  void setDim(std::pair<int, int> dim) { plane_dim = dim; }
  void setPosition(std::pair<int, int> pos) { plane_pos = pos; }
  
private:
  std::shared_ptr<Controller> controller;
  std::pair<int, int> plane_dim, plane_pos;
};

#endif
