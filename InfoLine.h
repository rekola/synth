#ifndef _INFOLINE_H_
#define _INFOLINE_H_

#include "UIElement.h"
#include "Controller.h"
#include "Synth.h"

#include <fmt/core.h>

class InfoLine : public UIElement {
 public:
  InfoLine(UIPlane & parent) : UIElement(parent) {
    setBgColor(120, 120, 120);
    setFgColor(30, 30, 30);
  }

  bool render(bool refresh = false) {
    auto & synth = getController().getSynth();
    auto & song = getController().getSong();
    
    int seconds = (int)synth.gettime(song);
    int minutes = seconds / 60;
    seconds %= 60;

    auto [ rows, cols ] = getDim();

    size_t section_index = synth.getTrackPosition();
    // auto & section = song.getSection();

    auto s = fmt::format(" {:02x} {:02d}:{:02d} section:{}", synth.getCurrentPosition(), minutes, seconds, section_index);
    while (s.size() < cols) s += ' ';
    
    putstr(0, 0, s);
    return true;
  }
};

#endif
