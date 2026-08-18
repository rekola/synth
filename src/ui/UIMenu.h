#ifndef _UIMENU_H_
#define _UIMENU_H_

#include "UIElement.h"

#include <string>
#include <vector>

class UIMenu : public UIElement {
 public:
  UIMenu() { }

  // Rebuilds the Buffers section's item list against `names` (every
  // currently open buffer/song, in Controller::getBufferNames() order,
  // used as each item's own "switch-to-buffer:<name>" command target),
  // the same-order `display_names` (Controller::getBufferDisplayName()'s
  // own already-disambiguated text for each - computed by the caller, not
  // here: unlike UIElement's other subclasses, UIMenu is never constructed
  // against a real UIPlane/parent, so getController() has no plane to
  // reach one through and would crash if called from here), and
  // `active_name` (Controller::getActiveBufferName(), so the section can
  // mark which one is current) - called once at startup and again
  // whenever the open-buffer set or the active buffer changes (see
  // Controller::setBufferChangeListener()'s one wiring in UI.cpp). Named
  // for what it does rather than "rebuild"/"refresh" alone since every
  // other section here is fixed at compile time; only Buffers needs this
  // at all.
  virtual void refreshBuffers(const std::vector<std::string> & names, const std::vector<std::string> & display_names,
			       const std::string & active_name) = 0;

  // The menu bar's own plane is created before the scope charts/pattern
  // editor/status line (TerminalUI::initialize()), so without this those
  // later, higher-z-order planes paint over an unrolled section's dropdown
  // wherever their rows happen to overlap it (row 1+, under the "File"
  // header row itself) - the dropdown is still tracked correctly
  // internally (ncmenu_selected() reflects it), it just never becomes
  // visible. The scope charts' own plot planes are created lazily (first
  // real sample data) and recreated on every resize, each occurrence
  // landing back on top of the menu, so this needs calling every frame,
  // not just once at startup - see its TerminalUI call sites.
  virtual void raiseToTop() = 0;

  // The command name bound to whichever item the most recent offerInput()
  // call activated (a click landing on an item, or Enter while one is
  // highlighted) - empty if nothing was activated. Cleared by the read, so
  // it's only ever acted on once. ncmenu has no notion of a command
  // attached to an item (just display text), so mapping description ->
  // command name, and detecting activation at all, is this app's own job -
  // see the TerminalMenu::offerInput() implementation.
  virtual std::string takeActivatedCommand() = 0;

 private:
};

#endif
