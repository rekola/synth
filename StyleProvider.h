#ifndef _STYLEPROVIDER_H_
#define _STYLEPROVIDER_H_

#include "UIColor.h"

#include <string>

class StyleProvider {
 public:
  UIColor window_border_color = "#ffffff";

  UIColor highlight_fg_color = "#000000";
  UIColor highlight_bg_color = "#a0ffa0";

  UIColor window_fg_color = "#e0e0e0";
  UIColor window_bg_color = "#202020";
};

#endif

