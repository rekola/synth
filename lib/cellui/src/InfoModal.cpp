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

void InfoModal::keyDown(const KeyDown & e) {
  // Only a press: the Enter that opened it is released after it is up.
  if (e.key == Key::Escape || e.key == Key::Enter || e.key == 'q' || (e.mods.ctrl && e.key == 'g')) done(0);
}

}  // namespace cellui
