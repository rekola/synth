#pragma once

namespace cellui {

// The shape of the mouse pointer over a plane (Plane::setMouseCursor()).
enum class MouseCursor {
  INHERIT,  // not asked for: whatever is below the plane shows
  DEFAULT,  // the arrow
  POINTER,  // a hand: what is under it takes a click
  TEXT,     // a text field
  GRABBING, // holding something being dragged
  NOT_ALLOWED, // what is dragged cannot go where the pointer is
};

// The name a display knows the shape by (CSS cursor names, which GTK and
// the terminals that set the pointer's shape both use).
constexpr const char * mouseCursorName(MouseCursor cursor) {
  switch (cursor) {
    case MouseCursor::POINTER: return "pointer";
    case MouseCursor::TEXT: return "text";
    case MouseCursor::GRABBING: return "grabbing";
    case MouseCursor::NOT_ALLOWED: return "not-allowed";
    case MouseCursor::INHERIT:
    case MouseCursor::DEFAULT: break;
  }
  return "default";
}

}  // namespace cellui
