#pragma once

#include <cellui/Plane.h>

#include <memory>
#include <optional>
#include <string>

namespace cellui {

// A text box on its own raised plane, no border,, its content Markdown (Markdown.h):
// the cell rendering of an info dialog, which a backend that can draw text
// more richly replaces by laying out markdown::parse() itself. Used both as
// a modal dialog and as a details popup.
class InfoDialog {
 public:
  bool isOpen() const { return plane_ != nullptr; }

  // (Re)draws the box under `parent`, `width` columns wide and at most
  // `max_rows` tall (extra text is cut). Call move() after it to place it.
  void show(Plane & parent, const std::string & title, const std::string & source, int width, int max_rows);
  // Screen position of the box's top-left corner.
  void move(int y, int x);
  // Centers the box on a screen of `screen_rows` x `screen_cols`.
  void center(int screen_rows, int screen_cols) {
    move((screen_rows - rows_) / 2, (screen_cols - width_) / 2);
  }
  void close() { plane_.reset(); }
  // How high it floats (Plane::setElevation()): the theme's overlay
  // elevation unless set; a modal dialog is set to the modal one.
  void setElevation(float elevation) {
    elevation_ = elevation;
    if (plane_ && elevation_) plane_->setElevation(*elevation_);
  }
  // Above every plane under the same parent.
  void raise() {
    if (plane_) plane_->moveToTop();
  }

  int rows() const { return rows_; }
  int width() const { return width_; }

 private:
  std::unique_ptr<Plane> plane_;
  std::optional<float> elevation_;
  int rows_ = 0, width_ = 0;
};

}  // namespace cellui
