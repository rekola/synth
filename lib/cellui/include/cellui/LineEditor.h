#pragma once

#include <cellui/Input.h>

#include <algorithm>
#include <string>
#include <vector>

namespace cellui {

// One line of editable text with Emacs editing keys: a sequence of grapheme
// clusters with a cursor between them, inserting what is typed. It draws
// nothing; a field asks it for the visible part at its width, scrolled to
// keep the cursor in view.
//
//   C-b/Left C-f/Right  a cluster      M-b M-f      a word
//   C-a/Home C-e/End    the line ends  Backspace    delete before
//   C-d/Delete          delete after   C-k C-u      kill to end, to start
//   C-w M-Backspace     kill word before, M-d kill word after
//   C-y                 yank (the kill text is shared by every field)
//   C-t                 transpose the clusters around the cursor
//
// Enter, C-g, Escape, Up, Down and Tab are left to the field's owner.
class LineEditor {
 public:
  void setText(const std::string & text);
  std::string text() const;
  // The cursor, in clusters from the start.
  size_t cursor() const { return cursor_; }
  void setCursor(size_t cursor) { cursor_ = std::min(cursor, clusters_.size()); }

  void insert(const std::string & text);
  // Returns whether the key was an editing key.
  bool offerInput(const Input & input);

  // What a field `width` columns wide shows: the text from the scroll
  // position, never splitting a wide cluster (a space stands in), and where
  // the cursor is in it.
  struct View {
    std::string text;
    int cursor_column = 0;
  };
  View view(int width);

  // The text the kill commands last took, shared by every editor.
  static std::string & killed();

 private:
  bool isWordCluster(size_t i) const;
  size_t wordBackward(size_t from) const;
  size_t wordForward(size_t from) const;
  void kill(size_t from, size_t to);
  int width(size_t from, size_t to) const;

  std::vector<std::string> clusters_;
  size_t cursor_ = 0;
  size_t scroll_ = 0;  // the first cluster shown
};

}  // namespace cellui
