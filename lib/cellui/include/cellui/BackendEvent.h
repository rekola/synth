#pragma once

#include <cellui/Input.h>

#include <string>

namespace cellui {

// One thing a backend reports to the application.
struct BackendEvent {
  enum class Type {
    INPUT,   // a key or a mouse event
    RESIZE,  // rows, cols hold the new size
    PASTE,   // text holds the pasted text
    CLOSE,   // the backend is gone: end of input, hang-up or a closed window
    DISPLAY, // the display's description changed: display() has the new one
  };

  Type type = Type::INPUT;
  Input input{ Key::Invalid, -1, -1, false, false, false, false };
  int rows = 0, cols = 0;
  std::string text;

  static BackendEvent makeInput(const Input & input) {
    BackendEvent e;
    e.input = input;
    return e;
  }
  static BackendEvent makeResize(int rows, int cols) {
    BackendEvent e;
    e.type = Type::RESIZE;
    e.rows = rows;
    e.cols = cols;
    return e;
  }
  static BackendEvent makePaste(std::string text) {
    BackendEvent e;
    e.type = Type::PASTE;
    e.text = std::move(text);
    return e;
  }
  static BackendEvent makeDisplay() {
    BackendEvent e;
    e.type = Type::DISPLAY;
    return e;
  }
  static BackendEvent makeClose() {
    BackendEvent e;
    e.type = Type::CLOSE;
    return e;
  }
};

}  // namespace cellui
