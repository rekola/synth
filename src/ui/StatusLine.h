#ifndef _STATUSLINE_H_
#define _STATUSLINE_H_

#include "UIElement.h"
#include "../playback/InputEvent.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

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
  // no-op is always the right response to "never mind". The reader always
  // claims the plane's full remaining width past the prompt - the
  // completion-status indicator (showPromptWithCompletion()/
  // showFilePrompt() below) doesn't need any width reserved for it, since
  // it's drawn directly onto the reader's own plane right after the typed
  // text rather than in a gap to its right - see showIndicator().
  void showPrompt(const std::string & prompt, std::function<void(const std::string &)> on_submit,
		   const std::string & initial_text = "") {
    on_submit_ = std::move(on_submit);
    getPlane().showReader(prompt, 0, -1, -1, -1, initial_text);
  }

  // Like showPrompt() above, but with Tab-driven Emacs-style autocomplete
  // against `candidates_for(text)` - every full valid answer starting with
  // whatever's typed so far, the exact same contract
  // Controller::commandCompletions() already uses for M-x (this is that
  // same machinery, generalized so any flat-candidate-set completion
  // domain can use it, not just M-x - buffer names being the other one).
  // Enter always submits whatever's currently typed outright, matched or
  // not: unlike M-x commands, a buffer name that doesn't exist yet is a
  // perfectly good answer (Controller::switchToBuffer() creates a fresh
  // buffer for it), so there's no "invalid input" to block here the way
  // showMx() below needs to. See completeAgainstSet() for Tab's own
  // behavior.
  void showPromptWithCompletion(const std::string & prompt, std::function<void(const std::string &)> on_submit,
				  std::function<std::set<std::string>(const std::string &)> candidates_for,
				  const std::string & initial_text = "") {
    tab_completer_ = [this, candidates_for]() { completeAgainstSet(candidates_for, false); };
    enter_completer_ = nullptr;
    indicator_shown_ = false;
    showPrompt(prompt, std::move(on_submit), initial_text);
  }

  // Like showPrompt(), but with filesystem-path Tab completion
  // (completeFilePath()) instead of a flat candidate set - unlike M-x
  // commands or buffer names, valid filenames aren't a fixed set to check
  // membership in, they're read from the real filesystem one path
  // component at a time. Enter always submits the typed text outright,
  // same reasoning as showPromptWithCompletion() above: a filename that
  // doesn't exist yet is still worth trying (openSong() reports "could not
  // open" itself if it's actually wrong), so there's nothing for this
  // class to block. "Find file:" (UI.cpp's open-song command) is the one
  // caller.
  void showFilePrompt(const std::string & prompt, std::function<void(const std::string &)> on_submit,
		       const std::string & initial_text = "") {
    tab_completer_ = [this]() { completeFilePath(); };
    enter_completer_ = nullptr;
    indicator_shown_ = false;
    showPrompt(prompt, std::move(on_submit), initial_text);
  }

  bool offerInput(const InputEvent & input) override {
    if (getPlane().readerActive()) {
      if (tab_completer_ && input.getId() == NCKEY_TAB) {
	// A local copy, not tab_completer_() directly - see the identical
	// reasoning on the Enter branch below.
	auto completer = tab_completer_;
	completer();
	return true;
      } else if (input.getId() == NCKEY_ENTER) {
	if (enter_completer_) {
	  // M-x only (the one strict completion domain - see showMx()):
	  // submit if the typed text is already a real, exact command name,
	  // otherwise extend/indicate exactly like Tab rather than running
	  // whatever's half-typed. A local copy, not enter_completer_()
	  // directly - its own submit path (completeAgainstSet() ->
	  // submitReader() -> closeReader()) reassigns tab_completer_/
	  // enter_completer_ to nullptr as part of finishing the reader
	  // session, which would destroy the very std::function target
	  // still executing if called through the member itself.
	  auto completer = enter_completer_;
	  completer();
	} else {
	  // No completer active, or a permissive one (buffer names, file
	  // paths - see showPromptWithCompletion()/showFilePrompt()) where
	  // Enter always accepts whatever's typed rather than only ever a
	  // confirmed match.
	  submitReader();
	}
	return true;
      } else if (input.hasCtrl() && input.getId() == 'g') {
	on_submit_ = nullptr;
	closeReader();
	return true;
      } else {
	// The user resumed typing - an indicator left over from an earlier
	// Tab press is now stale (a no-op when no completer is active or
	// nothing is currently shown).
	showIndicator("");
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
    tab_completer_ = nullptr;
    enter_completer_ = nullptr;
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
  // M-x itself is just showPromptWithCompletion()'s very first, default use
  // plus a stricter Enter (see enter_completer_'s own comment) - kept as
  // its own tiny wrapper so all three trigger paths above share one exact
  // prompt string.
  void showMx() {
    auto candidates_for = [this](const std::string & prefix) { return getController().commandCompletions(prefix); };
    tab_completer_ = [this, candidates_for]() { completeAgainstSet(candidates_for, false); };
    enter_completer_ = [this, candidates_for]() { completeAgainstSet(candidates_for, true); };
    indicator_shown_ = false;
    showPrompt("M-x ", [this](const std::string & cmd) { getController().sendCommand(cmd); });
  }

  // Emacs-style autocomplete against a flat candidate set (M-x commands,
  // buffer names): extend the typed text as far as every candidate sharing
  // its prefix agrees, or flash "[No match]" if nothing shares the prefix
  // at all. A press that actually extends the text completes silently, no
  // indicator - "[Sole completion]" only shows up on a *second*, no-
  // progress press once the text already exactly matches the one
  // remaining candidate, the same way Emacs's own TAB behaves. Either way
  // this never submits by itself; the whole point is that only Enter ever
  // runs/opens/switches anything. `submit_on_exact` is true only for the
  // Enter-triggered call (see offerInput()) and only for M-x, the one
  // domain where an unrecognized name truly can't be submitted - if the
  // typed text is already a real, complete candidate, that's the one case
  // Enter is allowed to act immediately on rather than just extending like
  // Tab would. Never shows the full candidate list itself (a later phase's
  // job) - just the furthest unambiguous extension plus the two
  // indicators above.
  void completeAgainstSet(const std::function<std::set<std::string>(const std::string &)> & candidates_for,
			   bool submit_on_exact) {
    auto text = getPlane().getReaderContents();
    auto matches = candidates_for(text);
    if (submit_on_exact && matches.count(text)) {
      submitReader();
      return;
    }
    if (matches.empty()) {
      showIndicator("[No match]");
      return;
    }
    auto completed = longestCommonPrefix(matches);
    if (completed.size() > text.size()) {
      // This press made real progress - just complete silently, the same
      // way Emacs's own TAB does. "[Sole completion]" only shows up on a
      // *second*, no-progress press once there's nothing left to extend -
      // see the else branch below.
      getPlane().setReaderContents(completed);
      showIndicator("");
    } else {
      showIndicator(matches.size() == 1 ? "[Sole completion]" : "");
    }
  }

  // "Find file:"'s own Tab handling. Unlike completeAgainstSet()'s flat
  // candidate set, filesystem completion is inherently hierarchical: only
  // the last path component is completed at a time, against whatever
  // actually exists in its containing directory - completing into a
  // directory extends the typed text and keeps going (there's nothing to
  // "open" about a bare directory name), the same way Emacs's own
  // find-file behaves. Always permissive (see showFilePrompt()) - there's
  // no submit_on_exact variant here, Enter for a file path never routes
  // through this method at all, only Tab does.
  void completeFilePath() {
    namespace fs = std::filesystem;
    auto text = getPlane().getReaderContents();

    // Split into "directory to list" + "partial name to match against
    // that directory's own entries" - a trailing slash (or empty text)
    // means the whole thing already names a directory with no partial
    // filename yet to narrow by.
    std::string dir_part, partial;
    if (text.empty() || text.back() == '/') {
      dir_part = text;
    } else {
      fs::path typed(text);
      auto parent = typed.parent_path().string();
      dir_part = parent.empty() ? "" : parent + "/";
      partial = typed.filename().string();
    }

    std::error_code ec;
    std::vector<std::string> matches; // bare entry names; directories carry a trailing "/"
    for (auto & entry : fs::directory_iterator(dir_part.empty() ? "." : dir_part, ec)) {
      if (ec) break;
      auto name = entry.path().filename().string();
      if (name.compare(0, partial.size(), partial) != 0) continue;
      // Emacs's own find-file ignores backup files (a trailing "~") in its
      // completion candidates by default (completion-ignored-extensions) -
      // without this, "arptest1.xml" can never register as the sole match
      // for "arptest1" while its own "arptest1.xml~" backup sits right
      // next to it in the same directory, sharing the same prefix.
      if (!entry.is_directory() && !name.empty() && name.back() == '~') continue;
      matches.push_back(entry.is_directory() ? name + "/" : name);
    }

    if (matches.empty()) {
      showIndicator("[No match]");
      return;
    }

    std::string common = matches.front();
    for (auto & m : matches) {
      size_t i = 0;
      while (i < common.size() && i < m.size() && common[i] == m[i]) i++;
      common.resize(i);
    }
    auto completed = dir_part + common;
    if (completed.size() > text.size()) {
      // This press made real progress - just complete silently, same as
      // completeAgainstSet()'s own reasoning: "[Sole completion]" only
      // shows up on a *second*, no-progress press once there's nothing
      // left to extend.
      getPlane().setReaderContents(completed);
      showIndicator("");
    } else {
      showIndicator(matches.size() == 1 ? "[Sole completion]" : "");
    }
  }

  void submitReader() {
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

  // Draws/clears the completion-status indicator ("[No match]"/"[Sole
  // completion]") immediately after whatever's currently typed (Emacs
  // shows its own equivalent messages the same way, right after point in
  // the minibuffer, not off in a fixed spot on the row) - see
  // UIPlane::showReaderIndicator()'s own comment for why this needs its
  // own dedicated plane rather than being drawn onto the reader's. `text
  // == ""` clears it - a no-op outside a completing session (no
  // tab_completer_ set), and (when clearing) a no-op when nothing is
  // currently displayed there, so callers can call it unconditionally on
  // every keystroke without needing to track whether there's anything to
  // clear. Positioned one column *past* the cursor (text.size() + 1, not
  // text.size()), leaving the cursor's own cell alone entirely - the
  // indicator plane is raised above the reader to be visible at all, and
  // sitting directly on the cursor's own cell let that raised plane's own
  // (otherwise invisible, blank) styling tint the terminal's cursor
  // rendering itself.
  void showIndicator(const std::string & text) {
    if (!tab_completer_ || (text.empty() && !indicator_shown_)) return;
    if (text.empty()) {
      getPlane().hideReaderIndicator();
    } else {
      auto x = static_cast<int>(getPlane().getReaderContents().size()) + 1;
      getPlane().showReaderIndicator(x, text);
    }
    indicator_shown_ = !text.empty();
  }

  bool meta_pressed = false;
  bool indicator_shown_ = false;
  std::string pending_message;
  std::function<void(const std::string &)> on_submit_;
  // Set (via showPromptWithCompletion()/showFilePrompt()/showMx()) for the
  // duration of a completing reader session, cleared by closeReader() -
  // Tab always runs this instead of self-inserting a literal tab
  // character. Never submits by itself (see completeAgainstSet()'s own
  // comment on why "[Sole completion]" stops short of just picking it) -
  // only enter_completer_/submitReader() ever close the reader.
  std::function<void()> tab_completer_;
  // Set only for the one strict completion domain (M-x, see showMx()) -
  // left unset for permissive ones (buffer names, file paths), where
  // Enter's plain submitReader() fallback in offerInput() already does the
  // right thing (accept whatever's typed, matched or not) without needing
  // a domain-specific check first.
  std::function<void()> enter_completer_;
};

#endif
