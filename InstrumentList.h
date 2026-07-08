#ifndef _INSTRUMENTLIST_H_
#define _INSTRUMENTLIST_H_

#include "UIElement.h"

class StyleProvider;

class InstrumentList : public UIElement {
 public:
  InstrumentList(UIPlane & parent) : UIElement(parent) {

  }

  bool offerInput(const InputEvent & input) override;
  bool render(const StyleProvider & styles, bool refresh = false);

protected:
  void renderRow(const StyleProvider & styles, int row, bool highlight);

 private:
  int current_song_version = 0;
  int new_scroll_pos = 0, current_scroll_pos = 0;
  int new_cursor_row = 0, current_cursor_row = 0;
};

#endif
