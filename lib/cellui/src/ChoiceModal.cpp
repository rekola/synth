#include <cellui/ChoiceModal.h>

namespace cellui {

ChoiceModal::ChoiceModal(Widget & root, Plane & screen, EventLoop & loop, std::string title, std::vector<std::string> labels, int current)
    : ModalDialog(root, screen, loop) {
  dialog_.setElevation(screen.getStyles().theme.elevation.modal);
  dialog_.open(std::move(title), std::move(labels), current);
}

void ChoiceModal::layout(Plane & screen, int rows, int cols) {
  dialog_.show(screen, rows, cols);
}

void ChoiceModal::handle(ChoiceDialog::Result result) {
  switch (result) {
    case ChoiceDialog::Result::NONE: relayout(); break; // the selection moved
    case ChoiceDialog::Result::CHOSEN: done(dialog_.selected()); break;
    case ChoiceDialog::Result::CANCELLED: done(kClosed); break;
  }
}

void ChoiceModal::keyDown(const KeyDown & e) { handle(dialog_.key(e)); }
void ChoiceModal::mouseUp(const MouseUp & e) {
  if (e.button == 1) handle(dialog_.click(e.row, e.col));
}
void ChoiceModal::wheel(const Wheel & e) {
  if (e.dy != 0) handle(dialog_.wheel(e.dy > 0));
}

}  // namespace cellui
