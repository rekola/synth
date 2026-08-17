#ifndef _UIELEMENT_H_
#define _UIELEMENT_H_

#include "../playback/EventHandler.h"
#include "UIPlane.h"

#include "UIColor.h"
#include "KeyChord.h"
#include "Keymap.h"
#include "CommandRegistry.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <cassert>
#include <optional>
#include <vector>

class UIElement : public EventHandler {
 public:
  explicit UIElement() { }
  explicit UIElement(UIPlane & parent) {
    setPlane(parent.createChild());
  }
  virtual ~UIElement() { }

  virtual bool offerInput(const InputEvent & input) {
    if (plane_) {
      return plane_->offerInput(input);
    } else {
      return false;
    }
  }

  // Emacs-style named-command dispatch: subclasses populate keymap_/
  // commands_ (typically in their constructor) and call dispatchCommand()
  // from their own offerInput() wherever a migrated binding should take
  // effect. executeCommand() is the public entry point used to invoke a
  // command directly by name (e.g. from an M-x-style minibuffer).
  bool executeCommand(std::string_view name) { return commands_.execute(std::string(name)); }

  // Read-only counterpart to executeCommand() - every name in this
  // element's own registry starting with `prefix` (the M-x minibuffer's
  // autocomplete support; see UI::commandCompletions()).
  std::vector<std::string> commandNames(std::string_view prefix) const {
    return commands_.matching(std::string(prefix));
  }

  UIElement & putstr(int y, int x, const std::string & s) {
    if (plane_) plane_->putstr(y, x, s);
    return *this;
  }
  UIElement & putstr(int y, int x, char c) {
    if (plane_) {
      std::string s;
      s += c;
      plane_->putstr(y, x, s);
    }
    return *this;
  }
#if 0
  UIElement & putstrN(int y, int x, const std::string & s, int limit) {
    
  }
#endif
  UIElement & move(int y, int x) {
    if (plane_) plane_->move(y, x);
    return *this;
  }
  UIElement & resize(int rows, int cols) {
    if (plane_) plane_->resize(rows, cols);
    onResize();
    return *this;
  }
  UIElement & erase() {
    if (plane_) plane_->erase();
    return *this;
  }
  UIElement & fill() {
    if (plane_) {
      auto [rows, cols] = getDim();
      std::string s(static_cast<size_t>(cols), ' ');
      for (auto i = 0; i < rows; i++) plane_->putstr(i, 0, s);
    }
    return *this;
  }
  UIElement & setFgColor(int r, int g, int b) {
    if (plane_) plane_->setFgColor(r, g, b);
    return *this;
  }
  UIElement & setBgColor(int r, int g, int b) {
    if (plane_) plane_->setBgColor(r, g, b);
    return *this;
  }
  UIElement & setFgColor(UIColor color) {
    return setFgColor(color.getRed(), color.getGreen(), color.getBlue());
  }
  UIElement & setBgColor(UIColor color) {
    return setBgColor(color.getRed(), color.getGreen(), color.getBlue());
  }
  UIElement & setUnderline(bool b) {
    if (plane_) plane_->setUnderline(b);
    return *this;
  }

  std::pair<int, int> getPosition() const {
    if (plane_) return plane_->getPosition();
    else return std::pair(0, 0);
  }
  
  std::pair<int, int> getDim() const {
    if (plane_) return plane_->getDim();
    else return std::pair(0, 0);
  }
  
  Controller & getController() const { return *(plane_->getController()); }

protected:
  void setPlane(std::unique_ptr<UIPlane> plane) { plane_ = std::move(plane); }
  UIPlane & getPlane() { return *plane_; }

  // Called after every resize() (including the initial construction-time
  // resize/move calls in UI::layout()), so subclasses that cache geometry-
  // dependent state (e.g. a notcurses plot/visual object) can invalidate it.
  // `resize()` is not virtual (it's called through Chart*/UIElement*-typed
  // handles in UI.cpp), so this hook exists specifically to still reach
  // subclass-specific behavior on resize.
  virtual void onResize() { }

  bool dispatchCommand(const InputEvent & input) {
    // RELEASE now reaches offerInput() (see InputEvent::Kind's own doc
    // comment) - no keymap-bound command is meant to fire on key-up, so
    // ignore it here rather than have every widget's keymap guard against
    // it individually. REPEAT is untouched (still actionable, same as
    // before) - e.g. holding a transpose/kill-region binding should keep
    // repeating, unlike PatternEditor's own raw note-entry keys below,
    // which suppress repeat themselves.
    if (input.getKind() == InputEvent::Kind::RELEASE) return false;
    auto chord = KeyChord::pack(input);

    // Two-key Emacs-style prefix sequences (C-x C-s, C-x C-c, ...) - a
    // pending prefix always consumes the very next keystroke one way or
    // another, matching Emacs's own "C-x <undefined-key>" behavior of
    // aborting the sequence rather than letting the second key fall
    // through to whatever it would otherwise have done (e.g. self-insert
    // in a text field) - a half-typed prefix should never silently do
    // something else instead.
    if (pending_prefix_) {
      auto prefix = *pending_prefix_;
      pending_prefix_.reset();
      if (auto name = keymap_.lookupPrefixed(prefix, chord)) return commands_.execute(*name);
      return true;
    }
    if (keymap_.isPrefix(chord)) {
      pending_prefix_ = chord;
      return true;
    }

    if (auto name = keymap_.lookup(chord)) return commands_.execute(*name);
    return false;
  }

  // Call after populating keymap_/commands_ in a subclass constructor to
  // catch a typo'd command name immediately instead of as a silently dead
  // keybinding.
  void assertCommandBindingsValid() const { assert(keymap_.allBoundIn(commands_)); }

  Keymap keymap_;
  CommandRegistry commands_;

  // Set by dispatchCommand() when the previous keystroke was a bound
  // prefix chord (e.g. C-x) - unset (no pending sequence) the rest of the
  // time. Per-instance, not shared - each widget that adopts prefix
  // bindings tracks its own in-progress sequence independently.
  std::optional<uint64_t> pending_prefix_;

private:
  std::unique_ptr<UIPlane> plane_;
};

#endif
