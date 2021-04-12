#ifndef _INFOLINE_H_
#define _INFOLINE_H_

#include "UIElement.h"
#include "Synth.h"

#include <fmt/core.h>

class InfoLine : public UIElement {
 public:
  InfoLine(UIPlane & parent) : UIElement(parent) { }

  bool render(Synth & synth, bool refresh = false) {
    putstr(0, 0, fmt::format("{:02x}", synth.getCurrentPosition()));
    return true;
  }
};

#endif
