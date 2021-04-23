#ifndef _INSTRUMENTLIST_H_
#define _INSTRUMENTLIST_H_

#include "UIElement.h"

class InstrumentList : public UIElement {
 public:
  InstrumentList(UIPlane & parent) : UIElement(parent) {

  }

  bool offerInput(const UIInput & input) override;
  bool render(bool refresh = false);

protected:
  void renderRow(size_t scroll_pos, size_t row, bool highlight);

 private:
  int current_song_version = 0;
  size_t new_scroll_pos = 0, current_scroll_pos = 0;
  size_t new_cursor_row = 0, current_cursor_row = 0;
};

#endif
