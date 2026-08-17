#ifndef _STATUSLINE_H_
#define _STATUSLINE_H_

#include "UIElement.h"
#include "../playback/InputEvent.h"

#include <algorithm>
#include <functional>
#include <set>
#include <string>

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
  // no-op is always the right response to "never mind". `reserved_cols`
  // leaves that many columns unused at the row's right edge instead of
  // letting the (opaque) reader plane claim the full remaining width - M-x
  // below uses it to leave room for the "[No match]" autocomplete
  // indicator; every other caller leaves it at 0 (the old full-width
  // behavior).
  void showPrompt(const std::string & prompt, std::function<void(const std::string &)> on_submit,
		   const std::string & initial_text = "", int reserved_cols = 0) {
    on_submit_ = std::move(on_submit);
    int reader_cols = -1;
    if (reserved_cols > 0) {
      auto [rows, cols] = getDim();
      reader_cols = std::max(1, cols - static_cast<int>(prompt.size()) - reserved_cols);
    }
    getPlane().showReader(prompt, 0, -1, -1, reader_cols, initial_text);
  }

  bool offerInput(const InputEvent & input) override {
    if (getPlane().readerActive()) {
      if (mx_active_ && (input.getId() == NCKEY_TAB || input.getId() == NCKEY_ENTER)) {
	completeMx();
	return true;
      } else if (input.getId() == NCKEY_ENTER) {
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
	// The user resumed typing - a "[No match]" indicator left over from
	// an earlier dead-end Tab/Enter is now stale (a no-op when mx_active_
	// is false or nothing is currently shown).
	showNoMatch(false);
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
    mx_active_ = false;
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
  // prompt string, plus the reserved width for completeMx()'s "[No match]"
  // indicator below. Enter only ever reaches sendCommand() with a name
  // completeMx() has already confirmed is real (see its exact-match
  // branch), so unlike before there's no failure case left to report here.
  void showMx() {
    mx_active_ = true;
    no_match_shown_ = false;
    showPrompt("M-x ", [this](const std::string & cmd) { getController().sendCommand(cmd); },
	       "", static_cast<int>(kNoMatchReserve));
  }

  // Emacs-style autocomplete, shared by Tab and Enter (see offerInput()):
  // extend the typed text as far as every command name sharing its prefix
  // agrees, submit outright if it already names one exactly, or flash
  // "[No match]" if nothing shares the prefix at all. Never shows the
  // candidate list itself (a later phase's job) - just the furthest
  // unambiguous extension.
  void completeMx() {
    auto text = getPlane().getReaderContents();
    auto matches = getController().commandCompletions(text);
    if (matches.count(text)) {
      submitMx();
      return;
    }
    if (matches.empty()) {
      showNoMatch(true);
      return;
    }
    showNoMatch(false);
    auto completed = longestCommonPrefix(matches);
    if (completed.size() > text.size()) getPlane().setReaderContents(completed);
    // Otherwise every match still disagrees past what's already typed
    // (ambiguous, e.g. "save-song" vs. "save-song-as") - leave the text
    // exactly as the user typed it, same as Emacs's own dead end short of
    // showing a completion list.
  }

  void submitMx() {
    auto text = closeReader();
    auto submit = std::move(on_submit_);
    on_submit_ = nullptr;
    if (submit) submit(text);
  }

  // names is sorted (a std::set) - the common prefix of the whole set is
  // exactly the common prefix of its first and last elements, so there's
  // no need to compare every pair.
  static std::string longestCommonPrefix(const std::set<std::string> & names) {
    const std::string & a = *names.begin();
    const std::string & b = *names.rbegin();
    size_t n = std::min(a.size(), b.size());
    size_t i = 0;
    while (i < n && a[i] == b[i]) i++;
    return a.substr(0, i);
  }

  // Draws/clears the "[No match]" indicator in the columns showMx()
  // reserved for it - a no-op outside an M-x session, and (for `show ==
  // false`) a no-op when nothing is currently displayed there, so callers
  // can call it unconditionally on every keystroke without needing to
  // track whether there's anything to clear.
  void showNoMatch(bool show) {
    if (!mx_active_ || (!show && !no_match_shown_)) return;
    auto [rows, cols] = getDim();
    auto x = cols - static_cast<int>(kNoMatchReserve);
    if (x < 0) return;
    if (show) {
      putstr(0, x, " [No match]");
    } else {
      putstr(0, x, std::string(kNoMatchReserve, ' '));
    }
    no_match_shown_ = show;
  }

  // Width of " [No match]" (11), reserved at the row's right edge for the
  // whole M-x session - see showPrompt()'s reserved_cols and showNoMatch()
  // above.
  static constexpr size_t kNoMatchReserve = 11;

  bool meta_pressed = false;
  bool mx_active_ = false;
  bool no_match_shown_ = false;
  std::string pending_message;
  std::function<void(const std::string &)> on_submit_;
};

#endif
