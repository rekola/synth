#ifndef _PATTERNEDITOR_H_
#define _PATTERNEDITOR_H_

#include "UIElement.h"

class Synth;
class UIInput;
class StyleProvider;

class PatternEditor : public UIElement {
 public:
  PatternEditor(UIPlane & parent);

  bool render(const StyleProvider & styles, bool refresh = false);
  bool offerInput(const UIInput & input) override;

protected:
  void renderHeading(const StyleProvider & styles);
  void renderRow(const StyleProvider & styles, size_t row, bool highlight);
  
  size_t current_score_playing_row = 0;
  size_t current_score_pattern = 0;
  size_t current_score_cursor_col = 0;
  size_t new_score_cursor_col = 0;
  bool row_edited = false;
  int current_song_version = 0;
};

#endif
