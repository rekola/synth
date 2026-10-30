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

void ChoiceModal::offerInput(const Input & input) {
  switch (dialog_.offerInput(input)) {
    case ChoiceDialog::Result::NONE: relayout(); break; // the selection moved
    case ChoiceDialog::Result::CHOSEN: done(dialog_.selected()); break;
    case ChoiceDialog::Result::CANCELLED: done(kClosed); break;
  }
}

}  // namespace cellui
