#ifndef _CHOICELIST_H_
#define _CHOICELIST_H_

#include <algorithm>

// The selection and scroll position of a list a person picks one entry from -
// the part of a choice dialog that doesn't depend on any toolkit. Movement
// stops at the ends rather than wrapping, since the first and last entries of
// a long list are the ones people overshoot to.
class ChoiceList {
 public:
  void reset(int count, int selected) {
    count_ = std::max(0, count);
    first_ = 0;
    selected_ = 0;
    select(selected);
  }

  int count() const { return count_; }
  int selected() const { return selected_; }

  void select(int index) {
    selected_ = count_ == 0 ? 0 : std::clamp(index, 0, count_ - 1);
  }
  void move(int delta) { select(selected_ + delta); }
  void home() { select(0); }
  void end() { select(count_ - 1); }

  // Index of the first entry shown in a window of `rows` rows. Scrolls only
  // as far as needed to keep the selection in view, so the window doesn't
  // jump while the selection moves inside it.
  int firstVisible(int rows) {
    rows = std::max(1, rows);
    if (selected_ < first_) first_ = selected_;
    if (selected_ >= first_ + rows) first_ = selected_ - rows + 1;
    first_ = std::clamp(first_, 0, std::max(0, count_ - rows));
    return first_;
  }

  // Scrolls the window without moving the selection (the mouse wheel); the
  // selection is pulled into view only if the scroll would leave it outside.
  void scroll(int delta, int rows) {
    rows = std::max(1, rows);
    first_ = std::clamp(first_ + delta, 0, std::max(0, count_ - rows));
    selected_ = count_ == 0 ? 0 : std::clamp(selected_, first_, std::min(count_ - 1, first_ + rows - 1));
  }

 private:
  int count_ = 0;
  int selected_ = 0;
  int first_ = 0;
};

#endif
