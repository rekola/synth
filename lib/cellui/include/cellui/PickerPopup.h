#pragma once

#include <memory>
#include <string>
#include <vector>

#include <cellui/Plane.h>

namespace cellui {

// A small dropdown over its parent plane: an opaque panel with a row of
// padding above and below, the items right-aligned in one column, a
// selection that wraps around at both ends. It knows no input device: the
// owner passes the keys and the pointer in as plain values, in the
// coordinates of the parent plane, and acts on the result.
class PickerPopup {
 public:
  enum class Result { IGNORED,    // not for the picker
                      MOVED,      // taken: the selection moved (or nothing to do)
                      CHOSEN,     // Enter, or a click on an item: read selection()
                      CANCELLED };  // cancel key, or a click outside

  bool isOpen() const { return plane_ != nullptr; }

  // Opens at (y, x) of `parent`, `rows` high including the padding rows and
  // `cols` wide; `selected` is where the highlight starts.
  void open(Plane & parent, int y, int x, int rows, int cols, std::vector<std::string> items, int selected);
  void close();

  const std::string & selection() const;
  int selectedIndex() const { return selected_; }

  // Enter chooses; the cancel key (Ctrl-G) cancels; Up/Down/PageUp/PageDown move.
  Result key(int key, bool ctrl);
  // Up (-1) or Down (+1) on the wheel.
  Result wheel(int direction);
  // A click released at (y, x) of the parent plane.
  Result click(int y, int x);

 private:
  void draw();
  void move(int delta);

  std::unique_ptr<Plane> plane_;
  std::vector<std::string> items_;
  int selected_ = 0;
  int first_ = 0;  // the first item shown
  int y_ = 0, x_ = 0, rows_ = 0, cols_ = 0;
};

}  // namespace cellui
