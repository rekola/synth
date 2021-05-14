#ifndef _INFOLINE_H_
#define _INFOLINE_H_

#include "UIElement.h"
#include "Controller.h"
#include "SongState.h"

#include <fmt/core.h>

class InfoLine : public UIElement {
 public:
  InfoLine(UIPlane & parent) : UIElement(parent) {
    setBgColor(120, 120, 120);
    setFgColor(30, 30, 30);
  }

  bool render(const StyleProvider & styles, bool refresh = false) {
    auto & info = getController().getPlaybackInfo();
    auto & song = getController().getSong();

    int new_version = song.getVersion();
    size_t new_position = info.getAbsolutePosition();
    
    if (refresh || new_version != current_version || new_position != current_position) {
      int seconds = (int)info.getTime();
      int minutes = seconds / 60;
      seconds %= 60;
      
      auto [ rows, cols ] = getDim();
      
      size_t section_index = info.getPatternPosition();
      size_t num_voices = info.getVoiceCount();
      size_t num_allocated_voices = info.getAllocatedVoiceCount();
      
      auto s = fmt::format(" {:02x} {:02d}:{:02d} pattern:{} voices:{}/{}", info.getAbsolutePosition(), minutes, seconds, section_index, num_voices, num_allocated_voices);
      while (s.size() < cols) s += ' ';
      
      putstr(0, 0, s);

      current_version = new_version;
      current_position = new_position;

      return true;
    } else {
      return false;
    }
  }

private:
  size_t current_position = 0;
  int current_version = 0;
};

#endif
