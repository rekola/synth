#ifndef _STATUSLINE_H_
#define _STATUSLINE_H_

#include "UIElement.h"
#include "InputEvent.h"

class StatusLine : public UIElement {
 public:
  StatusLine(UIPlane & parent) : UIElement(parent) { }

  bool isReaderActive() { return getPlane().readerActive(); }

  void setMessage(std::string s) {
    if (getPlane().readerActive()) {
      pending_message = std::move(s);
    } else {
      erase();
      putstr(0, 0, s.c_str());
    }
  }

  bool offerInput(const InputEvent & input) override {
    if (getPlane().readerActive()) {
      if (input.getId() == NCKEY_ENTER) {
	auto cmd = closeReader();
	if (!getController().sendCommand(cmd)) {
	  setMessage("Invalid command");
	}
	return true;
      } else if (input.hasCtrl() && input.getId() == 'g') {
	closeReader();
	return true;
      } else {
	return UIElement::offerInput(input);	
      }
    } else if ((input.hasAlt() || input.hasMeta()) && (input.getId() == 'x' || input.getId() == 'X')) {
      // Some terminals send the same wire bytes for physical Alt-x and for
      // Esc-then-x, and depending on protocol negotiation notcurses can
      // resolve that into a single alt/meta-modified 'x' event instead of
      // the two separate events the state machine below expects - handle
      // that directly rather than requiring the two-step path.
      getPlane().showReader("M-x ");
      return true;
    } else if (input.hasCtrl() && input.getId() == 'k') {
      // Reliable alternative to Esc-then-x/Alt-x: on GNOME Terminal (VTE,
      // no Kitty keyboard protocol) neither of the paths above ever fires -
      // ESC gets silently dropped by notcurses's own escape-sequence lexer
      // rather than played back as a literal keystroke as documented, so
      // 'x' arrives alone, unmodified, and is treated as a note instead of
      // opening M-x. Ctrl-K is an ordinary control byte, unambiguous on any
      // terminal - same fix pattern as Ctrl-B for Ctrl-SPC above. Mirrors
      // the "command palette" convention other editors use for the same
      // Alt/Meta-key-reliability reason (e.g. VS Code's Ctrl-Shift-P).
      // (Ctrl-P was tried first but never reaches the app at all - notcurses
      // never returns an ncinput event for raw byte 0x10 in this environment;
      // confirmed with a stderr trace at the earliest possible point, before
      // any app-level logic - see todo.txt's known-bugs section.)
      getPlane().showReader("M-x ");
      return true;
    } else if (input.getId() == NCKEY_ESC) {
      meta_pressed = true;
      return true;
    } else if (meta_pressed) {
      if (input.getId() == 'x' || input.getId() == 'X') {
    	getPlane().showReader("M-x ");
	return true;
      }
      meta_pressed = false;
    }
    return false;
  }

  std::string closeReader() {
    auto cmd = getPlane().closeReader();
    if (pending_message.empty()) {
      setMessage("");
    } else {
      setMessage(std::move(pending_message));
      pending_message.clear();
    }
    return cmd;
  }
  
private:
  bool meta_pressed = false;
  std::string pending_message;
};

#endif
