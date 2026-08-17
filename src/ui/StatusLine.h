#ifndef _STATUSLINE_H_
#define _STATUSLINE_H_

#include "UIElement.h"
#include "../playback/InputEvent.h"

#include <functional>

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

  // Opens the reader with `prompt`, running `on_submit` with whatever text
  // was typed once Enter is pressed - the one path every reader-driven
  // interaction goes through (M-x below, and UI.cpp's "New"/"Open"
  // unsaved-changes confirmation and filename prompts), rather than each
  // caller reimplementing "show a reader, do something with its text on
  // Enter". Cancelled (Ctrl-G) without ever running on_submit at all - a
  // no-op is always the right response to "never mind".
  void showPrompt(const std::string & prompt, std::function<void(const std::string &)> on_submit,
		   const std::string & initial_text = "") {
    on_submit_ = std::move(on_submit);
    getPlane().showReader(prompt, 0, -1, -1, -1, initial_text);
  }

  bool offerInput(const InputEvent & input) override {
    if (getPlane().readerActive()) {
      if (input.getId() == NCKEY_ENTER) {
	auto text = closeReader();
	auto submit = std::move(on_submit_);
	on_submit_ = nullptr;
	if (submit) submit(text);
	return true;
      } else if (input.hasCtrl() && input.getId() == 'g') {
	on_submit_ = nullptr;
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
      showMx();
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
      showMx();
      return true;
    } else if (input.getId() == NCKEY_ESC) {
      meta_pressed = true;
      return true;
    } else if (meta_pressed) {
      if (input.getId() == 'x' || input.getId() == 'X') {
    	showMx();
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
  // M-x itself is just showPrompt()'s very first, default use - kept as
  // its own tiny wrapper so all three trigger paths above share one exact
  // prompt string and one exact failure message, instead of repeating both.
  void showMx() {
    showPrompt("M-x ", [this](const std::string & cmd) {
      if (!getController().sendCommand(cmd)) setMessage("Invalid command");
    });
  }

  bool meta_pressed = false;
  std::string pending_message;
  std::function<void(const std::string &)> on_submit_;
};

#endif
