#ifndef _INPUTEVENT_H_
#define _INPUTEVENT_H_

#include "Event.h"
#include <cellui/Input.h>

// The key/mouse data is cellui::Input; this adds the app's event dispatch.
class InputEvent : public Event, public cellui::Input {
 public:
  using cellui::Input::Input;

  void dispatch(EventHandler & evh) override { evh.handleInputEvent(*this); }
};

#endif
