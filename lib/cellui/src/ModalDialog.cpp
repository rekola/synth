#include <cellui/ModalDialog.h>

#include <cellui/InputEvents.h>

namespace cellui {

ModalDialog::~ModalDialog() {
  // A dialog destroyed while up (an exception, a test) must not stay the one
  // that has the input.
  if (root_.activeModal() == this) root_.setActiveModal(nullptr);
}

void ModalDialog::connect() {
  // Every event is the dialog's: the handlers sit on the root, ahead of the
  // widget an event is aimed at.
  connections_.push_back(root_.on<KeyDown>([this](const KeyDown & e, EventContext &) { keyDown(e); return Handled::yes; }, Phase::capture));
  connections_.push_back(root_.on<KeyUp>([this](const KeyUp & e, EventContext &) { keyUp(e); return Handled::yes; }, Phase::capture));
  connections_.push_back(root_.on<MouseDown>([](const MouseDown &, EventContext &) { return Handled::yes; }, Phase::capture));
  connections_.push_back(root_.on<MouseUp>([this](const MouseUp & e, EventContext &) { mouseUp(e); return Handled::yes; }, Phase::capture));
  connections_.push_back(root_.on<MouseMove>([](const MouseMove &, EventContext &) { return Handled::yes; }, Phase::capture));
  connections_.push_back(root_.on<Wheel>([this](const Wheel & e, EventContext &) { wheel(e); return Handled::yes; }, Phase::capture));
}

int ModalDialog::showModal() {
  ModalDialog * previous = root_.activeModal();
  root_.setActiveModal(this);
  result_ = kClosed;
  connect();
  relayout();
  auto frame = loop_.onFrame([this] {
    if (animating()) relayout();
  });
  // A turn must run to show it, with no event to start one.
  loop_.wake();
  loop_.run(this);
  frame.disconnect();
  connections_.clear();
  root_.setActiveModal(previous);
  scrim_.close();
  close();
  return result_;
}

void ModalDialog::done(int result) {
  result_ = result;
  loop_.quit();
}

void ModalDialog::relayout() {
  auto [rows, cols] = screen_.getDim();
  scrim_.show(screen_, rows, cols, screen_.getStyles().theme.modal_backdrop);
  layout(screen_, rows, cols);
}

}  // namespace cellui
