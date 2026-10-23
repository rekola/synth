#include <cellui/Scrim.h>

#include <string>

namespace cellui {

void Scrim::show(Plane & parent, int rows, int cols, const Backdrop & backdrop) {
  if (!backdrop.active() || rows <= 0 || cols <= 0) {
    close();
    return;
  }
  if (!plane_) {
    plane_ = parent.createChild();
    plane_->setMouseCursor(MouseCursor::DEFAULT);
  }
  plane_->setName("scrim");
  // Under the modal dialog it is drawn for, which floats at the same height
  // and is raised above it.
  plane_->setElevation(plane_->getStyles().theme.elevation.modal);
  plane_->makeTransparent();
  plane_->resize(rows, cols);
  plane_->move(0, 0);
  plane_->erase();
  plane_->setBgDefault();
  plane_->setBackdrop(backdrop);
  std::string blank(static_cast<size_t>(cols), ' ');
  for (int row = 0; row < rows; row++) plane_->putstr(row, 0, blank);
  plane_->moveToTop();
}

}  // namespace cellui
