#ifndef _UIELEMENT_H_
#define _UIELEMENT_H_

#include "../playback/InputEvent.h"
#include "Plane.h"

#include "../model/Color.h"
#include "KeyStroke.h"
#include "Keymap.h"
#include "CommandRegistry.h"

#include "StyleProvider.h"
#include <cellui/Widget.h>

class Controller;

// The library's widget, plus what ties it to the app: the app's input
// event and the controller.
class UIElement : public cellui::Widget {
 public:
  UIElement() { }
  explicit UIElement(Plane & parent) : cellui::Widget(parent) {}

  // The plane's theme with the tracker's roles; every plane of the app is made
  // with one (InteractiveUI's and HeadlessUI's styles_).
  const StyleProvider & synthStyles() const { return ::synthStyles(getPlane()); }

  Controller & getController() const { return *static_cast<Controller *>(getPlane().getAppContext().get()); }
};

#endif
