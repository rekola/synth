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
  virtual void setFgColor(int r, int g, int b) = 0;
  virtual void setBgColor(int r, int g, int b) = 0;
  virtual void setUnderline(bool b) = 0;
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
  // blank) rather than requiring a second call after this one.
  virtual void showReader(const std::string & prompt = "", int y = 0, int x = -1, int rows = -1, int cols = -1,
			   const std::string & initial_text = "") = 0;
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
  virtual void showPicker() = 0;
  virtual void addItem(const std::string & id, const std::string & description) = 0;
  virtual void clearItems() = 0;
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
