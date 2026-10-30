#include <cellui/InfoModal.h>

#include <algorithm>

namespace cellui {

InfoModal::InfoModal(Widget & root, Plane & screen, EventLoop & loop, std::string title, std::string markdown)
    : ModalDialog(root, screen, loop), title_(std::move(title)), markdown_(std::move(markdown)) {
  dialog_.setElevation(screen.getStyles().theme.elevation.modal);
}

void InfoModal::layout(Plane & screen, int rows, int cols) {
  dialog_.show(screen, title_, markdown_, std::min(60, cols), rows);
  dialog_.center(rows, cols);
}

void InfoModal::offerInput(const Input & input) {
  // Not a release: the Enter that opened it is released after it is up.
  if (input.getKind() == Input::Kind::RELEASE) return;
  auto id = input.getId();
  if (id == Key::Escape || id == Key::Enter || id == 'q' || (input.hasCtrl() && id == 'g')) done(0);
}

}  // namespace cellui
