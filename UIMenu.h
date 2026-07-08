#ifndef _UIMENU_H_
#define _UIMENU_H_

#include "UIElement.h"

class UIMenu : public UIElement {
 public:
  UIMenu() { }

  virtual std::string getSelected() const = 0;

 private:
};

#endif
