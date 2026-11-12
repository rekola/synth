#pragma once

#include <cellui/Color.h>

namespace cellui {

// The 16 colours of the standard terminal palette (0-7, then their bright forms 8-15) as the RGB values
// terminals draw them with by default. A theme made of these gets back the index it meant when the
// terminal encoder looks for the nearest palette colour, so what it names is what the terminal's own
// colours decide.
inline const Color & ansi16Color(int index) {
  static const Color table[16] = {
      Color("#000000"), Color("#cd0000"), Color("#00cd00"), Color("#cdcd00"), Color("#0000ee"), Color("#cd00cd"), Color("#00cdcd"), Color("#e5e5e5"),
      Color("#7f7f7f"), Color("#ff0000"), Color("#00ff00"), Color("#ffff00"), Color("#5c5cff"), Color("#ff00ff"), Color("#00ffff"), Color("#ffffff"),
  };
  return table[index & 15];
}

}  // namespace cellui
