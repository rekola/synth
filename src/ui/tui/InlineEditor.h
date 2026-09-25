#ifndef _INLINEEDITOR_H_
#define _INLINEEDITOR_H_

#include "../../model/Color.h"

#include <functional>
#include <optional>
#include <string>

class UIPlane;
class InputEvent;

// A one-line, in-place text field over a widget's own cells (a track,
// clip or locator name). Enter commits through the callback
// given to open(), Ctrl-g cancels; while open, every other key goes to the
// reader rather than the owning widget's own bindings.
//
// The reader's own cells are background-transparent wherever nothing has
// been typed, so whatever the owner draws underneath shows through. The
// owner calls paintBackdrop() at the end of every render() pass to keep
// the field's span covered, and forces a full redraw whenever
// consumeRedrawRequest() says so (opening wipes the rest of the row, and
// closing leaves the blanked field behind).
class InlineEditor {
 public:
  struct Field {
    int row = 0, col = 0, width = 1;
    std::string initial_text;
    std::optional<Color> text_color; // the theme's reader_text_color if unset
    Color backdrop = Color(0, 0, 0);
  };

  explicit InlineEditor(UIPlane & plane) : plane_(plane) { }

  // A no-op returning false if a reader is already open on the owner's plane.
  bool open(const Field & field, std::function<void(std::string)> commit);
  bool isOpen() const;
  // Always consumes the input while open; returns false only when closed.
  bool offerInput(const InputEvent & input);
  // Closes without committing. A no-op if nothing is open.
  void cancel();
  void paintBackdrop();
  bool consumeRedrawRequest() {
    bool r = redraw_requested_;
    redraw_requested_ = false;
    return r;
  }

 private:
  void close();

  UIPlane & plane_;
  Field field_;
  std::function<void(std::string)> commit_;
  bool open_ = false;
  bool redraw_requested_ = false;
};

#endif
