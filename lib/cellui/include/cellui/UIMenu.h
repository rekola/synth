#pragma once

#include <cellui/Widget.h>
#include <cellui/StyleProvider.h>

#include <string>
#include <vector>

namespace cellui {

class UIMenu : public Widget {
 public:
  UIMenu() { }

  // Rebuilds the Buffers section's item list against `names` (every open
  // buffer, each used as its item's "switch-to-buffer:<name>" command
  // target), the same-order `display_names` (already disambiguated by the
  // caller: a menu is never constructed against a real plane, so it has no
  // app context to ask) and `active_name`, so the section can mark which one
  // is current. Called at startup and whenever the open-buffer set or the
  // active buffer changes; every other section is fixed at compile time.
  virtual void refreshBuffers(const std::vector<std::string> & names, const std::vector<std::string> & display_names,
			       const std::string & active_name) = 0;

  // Takes the menu bar's and its sections' colours from the theme.
  virtual void applyStyles(const StyleProvider & /*styles*/) { }

  // The menu bar's own plane is created before the widgets below it, so
  // without this those later, higher planes paint over an unrolled section's
  // dropdown wherever their rows overlap it - the dropdown is still tracked
  // correctly, it just never becomes visible. Planes that are created
  // lazily or recreated on every resize land back on top of the menu, so
  // this needs calling every frame, not just once at startup.
  virtual void raiseToTop() = 0;

  // The command name bound to whichever item the most recent offerInput()
  // call activated (a click landing on an item, or Enter while one is
  // highlighted) - empty if nothing was activated. Cleared by the read, so
  // it's only ever acted on once. A menu item has only display text, so mapping
  // description -> command name, and detecting activation at all, is the
  // implementation's job.
  virtual std::string takeActivatedCommand() = 0;

  // A section is unrolled and its dropdown showing.
  virtual bool isOpen() const { return false; }

 private:
};

}  // namespace cellui
