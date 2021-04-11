#ifndef _UI_H_
#define _UI_H_

#include "UIElement.h"

#include <string>

class UI : public UIElement {
 public:
  explicit UI() { }

  virtual void setStatus(const std::string & s) = 0;
};

#endif
