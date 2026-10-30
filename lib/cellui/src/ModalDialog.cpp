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
  auto take = [this](const Input * input) {
    if (input) offerInput(*input);
    return Handled::yes;
  };
  connections_.push_back(root_.on<KeyDown>([take](const KeyDown & e, EventContext &) { return take(e.input); }, Phase::capture));
  connections_.push_back(root_.on<KeyUp>([take](const KeyUp & e, EventContext &) { return take(e.input); }, Phase::capture));
  connections_.push_back(root_.on<MouseDown>([take](const MouseDown & e, EventContext &) { return take(e.input); }, Phase::capture));
  connections_.push_back(root_.on<MouseUp>([take](const MouseUp & e, EventContext &) { return take(e.input); }, Phase::capture));
  connections_.push_back(root_.on<MouseMove>([take](const MouseMove & e, EventContext &) { return take(e.input); }, Phase::capture));
  connections_.push_back(root_.on<Wheel>([take](const Wheel & e, EventContext &) { return take(e.input); }, Phase::capture));
}

int ModalDialog::exec() {
  ModalDialog * previous = root_.activeModal();
  root_.setActiveModal(this);
  result_ = kClosed;
  connect();
  relayout();
  auto frame = loop_.onFrame([this] {
    if (animating()) relayout();
  });
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
