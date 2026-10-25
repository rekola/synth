#pragma once

#include <cellui/Backdrop.h>
#include <cellui/Plane.h>

#include <memory>

namespace cellui {

// The layer a modal dialog sits on, at the theme's modal elevation: it
// covers the screen and shows everything below it through a backdrop, so
// what is behind reads as out of reach. It draws nothing of its own; raise the dialog above it after show().
class Scrim {
 public:
  bool isOpen() const { return plane_ != nullptr; }
  // (Re)covers a screen of `rows` x `cols` under `parent`; an inactive
  // backdrop shows nothing.
  void show(Plane & parent, int rows, int cols, const Backdrop & backdrop);
  void close() { plane_.reset(); }
  void raise() {
    if (plane_) plane_->moveToTop();
  }

 private:
  std::unique_ptr<Plane> plane_;
};

}  // namespace cellui
