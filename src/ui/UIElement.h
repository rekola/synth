#ifndef _UIELEMENT_H_
#define _UIELEMENT_H_

#include "../playback/EventHandler.h"
#include "../playback/InputEvent.h"
#include "UIPlane.h"

#include "../model/Color.h"
#include "KeyChord.h"
#include "Keymap.h"
#include "CommandRegistry.h"

#include <cellui/Element.h>

class Controller;

// The library's widget, plus what ties it to the app: the event handler
// interface, the app's input event and the controller.
class UIElement : public cellui::Element, public EventHandler {
 public:
  UIElement() { }
  explicit UIElement(UIPlane & parent) : cellui::Element(parent) { }

  Controller & getController() const { return *static_cast<Controller *>(getPlane().getAppContext().get()); }
};

#endif
