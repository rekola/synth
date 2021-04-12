#ifndef _STATUSLINE_H_
#define _STATUSLINE_H_

#include "UIElement.h"

class StatusLine : public UIElement {
 public:
  StatusLine(UIPlane & parent) : UIElement(parent) { }

  virtual void setMessage(const std::string & s) {
    erase();
    putstr(0, 0, s.c_str());
  }
};

#endif
