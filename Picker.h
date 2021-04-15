#ifndef _PICKER_H_
#define _PICKER_H_

#include "UIElement.h"

class Picker : public UIElement {
 public:
 Picker(UIPlane & plane, bool _is_multiselect = false) : UIElement(plane), is_multiselect(_is_multiselect) { }

  virtual void addItem(std::string label) = 0;

  bool isMultiSelect() const { return is_multiselect; }
  
 private:
  bool is_multiselect;
};

#endif

