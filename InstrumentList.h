#ifndef _INSTRUMENTLIST_H_
#define _INSTRUMENTLIST_H_

#include "UIElement.h"

class Instrument;

class InstrumentList : public UIElement {
 public:
  InstrumentList(UIPlane & parent) : UIElement(parent) {

  }

  bool render(bool refresh = false);

protected:
  void renderRow(size_t row, const Instrument & instrument, bool highlight);

 private:
  int current_song_version = 0;  
};

#endif
