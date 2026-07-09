#ifndef _STYLEPROVIDER_H_
#define _STYLEPROVIDER_H_

#include "UIColor.h"

class StyleProvider {
 public:
  UIColor highlight_fg_color = "#000000";
  UIColor highlight_bg_color = "#a0ffa0";

  UIColor window_border_color = "#323232";
  UIColor window_fg_color = "#9e9e9e";
  UIColor window_bg_color = "#151515";
  UIColor window_accent_fg_color = "#ffffff";
  UIColor window_accent_bg_color = "#292929";

  UIColor command_column_color = "#c67610";

  UIColor selection_fg_color = "#e0e8ff";
  UIColor selection_bg_color = "#35507a";
};

#endif

