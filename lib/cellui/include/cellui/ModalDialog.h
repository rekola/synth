#pragma once

#include <cellui/EventLoop.h>
#include <cellui/Input.h>
#include <cellui/Plane.h>
#include <cellui/Scrim.h>
#include <cellui/Signal.h>
#include <cellui/Widget.h>

#include <vector>

namespace cellui {

// The base of a modal dialog. A dialog is made on the stack and shown with
// exec(), which runs a nested event loop and returns with the dialog's
// result. Everything else is the dialog's own: the scrim behind it, taking
// every key and pointer event while it is up (the application's widgets get
// none), and ending when it calls done(). The application only needs to know
// that a modal dialog has the input (Widget::activeModal()) so as not to
// offer it the events itself.
class ModalDialog {
 public:
  // `root` is the widget tree the events come through, `screen` the plane the
  // dialog is drawn under, `loop` the one exec() runs.
  ModalDialog(Widget & root, Plane & screen, EventLoop & loop) : root_(root), screen_(screen), loop_(loop) { }
  virtual ~ModalDialog();
  ModalDialog(const ModalDialog &) = delete;
  ModalDialog & operator=(const ModalDialog &) = delete;

  // Result of a dialog that ended without done(): the loop was quit under it.
  static constexpr int kClosed = -1;

  // Shows the dialog and returns its result once it is done.
  int exec();
  // Ends exec() with `result`, after the turn it is called in.
  void done(int result);

  // Draws it again for the current screen size; the application calls it
  // when the screen is resized.
  void relayout();
  // Whether it is still moving, so another frame is due.
  virtual bool animating() const { return false; }
  // Whether the display's own text selection should work over it, instead of
  // the application getting the pointer.
  virtual bool wantsNativeSelection() const { return false; }

 protected:
  // (Re)draws the dialog on a screen of `rows` x `cols`, above the scrim.
  virtual void layout(Plane & screen, int rows, int cols) = 0;
  // Takes one key or pointer event.
  virtual void offerInput(const Input & input) = 0;
  // Removes the dialog's planes.
  virtual void close() = 0;

 private:
  void connect();

  Widget & root_;
  Plane & screen_;
  EventLoop & loop_;
  Scrim scrim_;
  int result_ = kClosed;
  std::vector<Connection> connections_;
};

}  // namespace cellui
