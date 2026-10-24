#pragma once

#include <string>

namespace cellui {

// The events a display's input becomes (see InputDispatcher). Plain structs,
// posted to widgets like any other event (Widget::post()).

struct Modifiers {
  bool shift = false, ctrl = false, alt = false, meta = false;
};

// A key went down. `key` is a Key:: id; `text` is what typing it inserts, in
// UTF-8, empty for a key that inserts nothing (arrows, or any shortcut with
// Ctrl, Alt or Meta).
struct KeyDown {
  int key = 0;
  Modifiers mods;
  std::string text;
  bool repeat = false;
};

// Only where DisplayInfo::key_release.
struct KeyUp {
  int key = 0;
  Modifiers mods;
};

// A pointer position: x and y in cells from the top left, with a fraction
// where DisplayInfo::precise_mouse, else whole; row and col are the cell.
struct MouseDown {
  float x = 0, y = 0;
  int row = 0, col = 0;
  int button = 1;  // 1 left, 2 middle, 3 right
  Modifiers mods;
};

// buttons: the held ones, bit n-1 for button n; 0 for plain hover.
struct MouseMove {
  float x = 0, y = 0;
  int row = 0, col = 0;
  unsigned buttons = 0;
  Modifiers mods;
};

struct MouseUp {
  float x = 0, y = 0;
  int row = 0, col = 0;
  int button = 1;
  Modifiers mods;
};

// dy > 0 is up (content moves down), dx > 0 is right, in steps of one notch.
struct Wheel {
  float x = 0, y = 0;
  int row = 0, col = 0;
  int dx = 0, dy = 0;
  Modifiers mods;
};

// The pointer came onto a widget (outermost first) or left it (deepest
// first). Sent to that widget only, never to its ancestors.
struct Enter {
  int row = 0, col = 0;
};
struct Leave {
  int row = 0, col = 0;
};

// The keyboard focus came to a widget or went from it. Sent to that widget
// only.
struct FocusIn { };
struct FocusOut { };

// The display changed size. Reaches every handler of the tree.
struct Resize {
  int rows = 0, cols = 0;
};

}  // namespace cellui
