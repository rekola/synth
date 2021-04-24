#ifndef _INSTRUMENTLIST_H_
#define _INSTRUMENTLIST_H_

#include "UIElement.h"
#include "InstrumentState.h"

class StyleProvider;

class InstrumentList : public UIElement {
 public:
  InstrumentList(UIPlane & parent) : UIElement(parent) {

  }

  bool offerInput(const UIInput & input) override;
  bool render(const StyleProvider & styles, bool refresh = false);

protected:
  void renderRow(const StyleProvider & styles, size_t scroll_pos, size_t row, bool highlight);

 private:
  int current_song_version = 0;
  size_t new_scroll_pos = 0, current_scroll_pos = 0;
  size_t new_cursor_row = 0, current_cursor_row = 0;
  InstrumentState instrument_state;
};

#endif
