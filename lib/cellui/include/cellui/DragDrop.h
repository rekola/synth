#pragma once

#include <cellui/InputEvents.h>
#include <cellui/MouseCursor.h>
#include <cellui/Plane.h>

#include <any>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace cellui {

class Widget;

// Drag and drop inside the application, where DisplayInfo::drag_drop. A
// widget that has the button held after a press starts a session by posting
// StartDrag to the root (Widget::postToRoot()); the InputDispatcher then
// runs it until the button is released or Escape is pressed:
//
// - the widget under the pointer gets DragOver on every move and accepts by
//   handling it (Handled::yes); it gets DragLeave when the pointer leaves it;
// - the source gets DragMove (to move its DragGhost) and, at the end,
//   DragEnd; nothing else reaches it, not even the release;
// - a release over a widget that accepted posts Drop to it.
//
// A display without drag_drop answers StartDrag with Handled::no; the
// widget then behaves as it did before.

// What is being dragged: `kind` tells a target what `data` holds, so it can
// refuse what it does not know.
struct DragPayload {
  std::string kind;
  std::any data;
  template <class T>
  const T * get() const { return std::any_cast<T>(&data); }
};

// Posted to the root by the source widget; Handled::yes when a session began.
struct StartDrag {
  Widget * source = nullptr;
  DragPayload payload;
};

struct DragOver {
  const DragPayload & payload;
  float x = 0, y = 0;
  int row = 0, col = 0;
  Modifiers mods;
};

struct DragLeave { };

struct Drop {
  const DragPayload & payload;
  float x = 0, y = 0;
  int row = 0, col = 0;
  Modifiers mods;
};

// To the source after each move: where the pointer is and whether the
// widget under it accepts what is dragged.
struct DragMove {
  float x = 0, y = 0;
  int row = 0, col = 0;
  Modifiers mods;
  bool accepted = false;
};

// To the source when the session ends; `dropped` when a target took it.
struct DragEnd {
  bool dropped = false;
};

// How far the pointer must move from the press (in cells, a column counting
// half a row) before a press becomes a drag.
constexpr float kDragThreshold = 0.5f;
inline bool dragStarted(float press_y, float press_x, float y, float x) {
  return std::hypot(y - press_y, (x - press_x) * 0.5f) >= kDragThreshold;
}

// What follows the pointer while a session runs: a plane above everything,
// `rows` by `cols` cells, which `draw` fills (again whenever acceptance
// changes). The point grabbed stays under the pointer; a display that
// scrolls by parts of a cell places it to the pixel. The pointer over it
// shows a grabbing hand, or a refusal where nothing accepts. The plane
// goes with the object. `parent` is the root plane (planes are flat).
class DragGhost {
 public:
  using Draw = std::function<void(Plane & plane, bool accepted)>;

  // grab_y/grab_x: where in the ghost (cells) the pointer took hold of it.
  DragGhost(Plane & parent, int rows, int cols, float grab_y, float grab_x, Draw draw)
    : draw_(std::move(draw)), grab_y_(grab_y), grab_x_(grab_x) {
    plane_ = parent.createChild();
    plane_->makeTransparent();
    plane_->setElevation(parent.getStyles().theme.elevation.modal);
    plane_->resize(rows, cols);
    paint();
    plane_->moveToTop();
  }

  // The pointer is at (y, x), in cells.
  void moveTo(float y, float x) {
    float top = y - grab_y_, left = x - grab_x_;
    int row = static_cast<int>(std::ceil(top)), col = static_cast<int>(std::ceil(left));
    // Shifted up and left by what the whole cell goes past.
    plane_->setScrollOffset(static_cast<double>(row) - top, static_cast<double>(col) - left);
    plane_->move(row, col);
    plane_->moveToTop();
  }

  void setAccepted(bool accepted) {
    if (accepted == accepted_) return;
    accepted_ = accepted;
    paint();
  }

 private:
  void paint() {
    plane_->erase();
    draw_(*plane_, accepted_);
    plane_->setMouseCursor(accepted_ ? MouseCursor::GRABBING : MouseCursor::NOT_ALLOWED);
  }

  std::unique_ptr<Plane> plane_;
  Draw draw_;
  float grab_y_, grab_x_;
  bool accepted_ = true;
};

}  // namespace cellui
