#ifndef _CHOICEDIALOG_H_
#define _CHOICEDIALOG_H_

#include "../ChoiceList.h"
#include "../UIElement.h"

#include <memory>
#include <string>
#include <vector>

class InputEvent;

// A modal, bordered list the person picks one entry from: the terminal
// rendering of UI::showChoiceDialog(). Up/Down/PageUp/PageDown/Home/End (and
// the Emacs-style next/previous-line keys) move, Enter or a click chooses,
// Escape or a click outside cancels; the mouse wheel scrolls.
class ChoiceDialog {
 public:
  enum class Result { NONE,
                      CHOSEN,
                      CANCELLED };

  bool isOpen() const { return plane_ != nullptr || !labels_.empty(); }

  // `current` is marked (the entry already in use) and is where the selection
  // starts, -1 for none. Nothing is drawn until show().
  void open(std::string title, std::vector<std::string> labels, int current);
  void close();

  // (Re)draws the box under `parent`, centred on a screen of the given size.
  // Call again after a resize or any change of selection.
  void show(UIPlane & parent, int screen_rows, int screen_cols);

  // Feeds one input event; the dialog swallows everything, so the caller
  // only needs the result.
  Result offerInput(const InputEvent & input);

  int selected() const { return list_.selected(); }

 private:
  int visibleRows() const { return rows_ - 2; }

  std::unique_ptr<UIPlane> plane_;
  std::string title_;
  std::vector<std::string> labels_;
  int current_ = -1;
  ChoiceList list_;
  // Where the box sits and how big it is, as of the last show().
  int y_ = 0, x_ = 0, rows_ = 0, width_ = 0;
};

#endif
