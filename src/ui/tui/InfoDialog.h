#ifndef _INFODIALOG_H_
#define _INFODIALOG_H_

#include "../UIElement.h"

#include <memory>
#include <string>

// A bordered text box on its own plane, its content Markdown (see
// ui/Markdown.h) - the terminal rendering of an info dialog. Used both as
// a modal dialog (About) and as the outline panel's details popup.
class InfoDialog {
 public:
  bool isOpen() const { return plane_ != nullptr; }

  // (Re)draws the box under `parent`, `width` columns wide and at most
  // `max_rows` tall (extra text is cut). Call move() after it to place it.
  void show(UIPlane & parent, const std::string & title, const std::string & source, int width, int max_rows);
  // Screen position of the box's top-left corner.
  void move(int y, int x);
  // Centers the box on a screen of `screen_rows` x `screen_cols`.
  void center(int screen_rows, int screen_cols) {
    move((screen_rows - rows_) / 2, (screen_cols - width_) / 2);
  }
  void close() { plane_.reset(); }

  int rows() const { return rows_; }
  int width() const { return width_; }

 private:
  std::unique_ptr<UIPlane> plane_;
  int rows_ = 0, width_ = 0;
};

#endif
