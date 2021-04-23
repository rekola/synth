#ifndef _PATTERNEDITOR_H_
#define _PATTERNEDITOR_H_

#include "UIElement.h"

#include <fmt/core.h>
#include <string>
#include <unordered_map>

class Synth;
class UIInput;

class PatternEditor : public UIElement {
 public:
  PatternEditor(UIPlane & parent);

  bool render(bool refresh = false);
  bool offerInput(const UIInput & input) override;

protected:
  void renderHeading();
  void renderRow(size_t row, bool highlight);

  std::string getNoteName(int note) const {
    auto it = midi_note_names.find(note);
    if (it != midi_note_names.end()) {
      return it->second;
    } else {
      return fmt::format("x{:02x}", note);
    }
  }
  
  size_t current_score_playing_row = 0;
  size_t current_score_section = 0;
  size_t current_score_cursor_col = 0;
  size_t new_score_cursor_col = 0;
  bool row_edited = false;
  int current_song_version = 0;
  
private:
  std::unordered_map<short, std::string> midi_note_names;  
};

#endif
