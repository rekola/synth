#ifndef _STYLEPROVIDER_H_
#define _STYLEPROVIDER_H_

#include "UIColor.h"

#include <string>

class StyleProvider {
 public:

  UIColor highlight_fg_color = "#000000";
  UIColor highlight_bg_color = "#a0ffa0";

  UIColor window_border_color = "#323232";
  UIColor window_fg_color = "#9e9e9e";
  UIColor window_bg_color = "#151515";
  UIColor window_accent_fg_color = "#ffffff";
  UIColor window_accent_bg_color = "#292929";
};

#endif

