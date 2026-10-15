#include <cellui/Scrim.h>

#include <string>

namespace cellui {

void Scrim::show(UIPlane & parent, int rows, int cols, const Backdrop & backdrop) {
  if (!backdrop.active() || rows <= 0 || cols <= 0) {
    close();
    return;
  }
  if (!plane_) plane_ = parent.createChild();
  plane_->setName("scrim");
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
