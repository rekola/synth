#ifndef _HIERARCHYVIEW_H_
#define _HIERARCHYVIEW_H_

#include "UIElement.h"

#include <vector>
#include <string>

class StyleProvider;

struct hierarchy_row_s {
  int level = 0;
  std::string label;  
};

class HierarchyView : public UIElement {
 public:
  HierarchyView(UIPlane & parent) : UIElement(parent) {

  }

  bool offerInput(const InputEvent & input) override;
  bool render(const StyleProvider & styles, bool refresh = false);

protected:
  void renderRow(const StyleProvider & styles, int row, bool highlight);

 private:
  std::vector<struct hierarchy_row_s> data_;
  int current_song_version_ = 0;
  int new_scroll_pos_ = 0, current_scroll_pos_ = 0;
  int new_cursor_row_ = 0, current_cursor_row_ = 0;
};

#endif
