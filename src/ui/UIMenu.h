#ifndef _UIMENU_H_
#define _UIMENU_H_

#include "UIElement.h"

class UIMenu : public UIElement {
 public:
  UIMenu() { }

  virtual std::string getSelected() const = 0;

  // The menu bar's own plane is created before the scope charts/pattern
  // editor/status line (TerminalUI::initialize()), so without this those
  // later, higher-z-order planes paint over an unrolled section's dropdown
  // wherever their rows happen to overlap it (row 1+, under the "File"
  // header row itself) - the dropdown is still tracked correctly
  // internally (StatusLine shows "menu: New" once an item is selected),
  // it just never becomes visible. The scope charts' own plot planes are
  // created lazily (first real sample data) and recreated on every resize,
  // each occurrence landing back on top of the menu, so this needs calling
  // every frame, not just once at startup - see its TerminalUI call sites.
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
