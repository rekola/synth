#ifndef _INFOLINE_H_
#define _INFOLINE_H_

#include "UIElement.h"
#include "Synth.h"

#include <fmt/core.h>

class InfoLine : public UIElement {
 public:
  InfoLine(UIPlane & parent) : UIElement(parent) {
    setBgColor(120, 120, 120);
    setFgColor(30, 30, 30);
  }

  bool render(Synth & synth, bool refresh = false) {
    int seconds = (int)synth.gettime();
    int minutes = seconds / 60;
    seconds %= 60;

    auto [ rows, cols ] = getDim();

    auto s = fmt::format(" {:02x} {:02d}:{:02d}", synth.getCurrentPosition(), minutes, seconds);
    while (s.size() < cols) s += ' ';
    
    putstr(0, 0, s);
    return true;
  }
};

#endif
