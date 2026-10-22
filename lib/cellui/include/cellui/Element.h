#pragma once

#include <cellui/Color.h>
#include <cellui/CommandRegistry.h>
#include <cellui/Input.h>
#include <cellui/KeyChord.h>
#include <cellui/KeyChordNames.h>
#include <cellui/Keymap.h>
#include <cellui/UIPlane.h>

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cellui {

// A widget: owns a plane, draws through it, and dispatches key chords to
// named commands.
class Element {
 public:
  explicit Element() { }
  explicit Element(UIPlane & parent) {
    setPlane(parent.createChild());
  }
  virtual ~Element() { }

  // Offers a key or mouse event to this widget; true when it was used. The
  // default passes it on to the plane.
  virtual bool offerInput(const Input & input) {
    return plane_ ? plane_->offerInput(input) : false;
  }

  // Emacs-style named-command dispatch: subclasses populate keymap_/
  // commands_ (typically in their constructor) and call dispatchCommand()
  // from their own offerInput() wherever a migrated binding should take
  // effect. executeCommand() is the public entry point used to invoke a
  // command directly by name (e.g. from an command palette).
  bool executeCommand(std::string_view name) { return commands_.execute(std::string(name)); }

  // Read-only counterpart to executeCommand() - every name in this
  // element's own registry starting with `prefix` (autocomplete support
  // for a command prompt).
  std::vector<std::string> commandNames(std::string_view prefix) const {
    return commands_.matching(std::string(prefix));
  }

  Element & putstr(int y, int x, const std::string & s) {
    if (plane_) plane_->putstr(y, x, s);
    return *this;
  }
  Element & putstr(int y, int x, char c) {
    if (plane_) {
      std::string s;
      s += c;
      plane_->putstr(y, x, s);
    }
    return *this;
  }
#if 0
  Element & putstrN(int y, int x, const std::string & s, int limit) {
    
  }
#endif
  Element & move(int y, int x) {
    if (plane_) plane_->move(y, x);
    return *this;
  }
  // See UIPlane::moveToTop()'s own comment - raises this widget's plane
  // above every sibling under the same parent.
  Element & moveToTop() {
    if (plane_) plane_->moveToTop();
    return *this;
  }
  Element & setVisible(bool visible) {
    if (plane_) plane_->setVisible(visible);
    return *this;
  }
  Element & resize(int rows, int cols) {
    if (plane_) plane_->resize(rows, cols);
    onResize();
    return *this;
  }
  Element & erase() {
    if (plane_) plane_->erase();
    return *this;
  }
  Element & fill() {
    if (plane_) {
      auto [rows, cols] = getDim();
      std::string s(static_cast<size_t>(cols), ' ');
      for (auto i = 0; i < rows; i++) plane_->putstr(i, 0, s);
    }
    return *this;
  }
  Element & setFgColor(int r, int g, int b) {
    if (plane_) plane_->setFgColor(r, g, b);
    return *this;
  }
  Element & setBgColor(int r, int g, int b) {
    if (plane_) plane_->setBgColor(r, g, b);
    return *this;
  }
  Element & setFgColor(Color color) {
    return setFgColor(color.getRed(), color.getGreen(), color.getBlue());
  }
  Element & setBgColor(Color color) {
    return setBgColor(color.getRed(), color.getGreen(), color.getBlue());
  }
  Element & setBgDefault() {
    if (plane_) plane_->setBgDefault();
    return *this;
  }
  Element & setUnderline(bool b) {
    if (plane_) plane_->setUnderline(b);
    return *this;
  }
  Element & setBold(bool b) {
    if (plane_) plane_->setBold(b);
    return *this;
  }
  Element & setItalic(bool b) {
    if (plane_) plane_->setItalic(b);
    return *this;
  }
  Element & setLift(float lift, bool joins_above = false) {
    if (plane_) plane_->setLift(lift, joins_above);
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
  
protected:
  void setPlane(std::unique_ptr<UIPlane> plane) { plane_ = std::move(plane); }
  UIPlane & getPlane() { return *plane_; }
  const UIPlane & getPlane() const { return *plane_; }
  bool hasPlane() const { return plane_ != nullptr; }

  // Called after every resize() (including the initial construction-time
  // resize/move calls), so subclasses that cache geometry-
  // dependent state (e.g. an image sized to the plane) can invalidate it.
  // `resize()` is not virtual (it's called through base-typed handles), so
  // this hook exists to still reach subclass-specific behavior on resize.
  virtual void onResize() { }

public:
  // How `command` is bound in this widget's keymap on the display's profile
  // ("C-x C-s", "Ctrl+S"), the simplest of its bindings; "" if it has none.
  std::string bindingFor(const std::string & command) const {
    auto profile = keymapProfile();
    std::vector<uint64_t> best;
    auto simpler = [](const std::vector<uint64_t> & a, const std::vector<uint64_t> & b) {
      auto mods = [](const std::vector<uint64_t> & seq) {
        int n = 0;
        for (auto chord : seq) n += (chord & 1u ? 1 : 0) + (chord & 2u ? 1 : 0) + (chord & 4u ? 1 : 0) + (chord & 8u ? 1 : 0);
        return n;
      };
      if (a.size() != b.size()) return a.size() < b.size();
      if (mods(a) != mods(b)) return mods(a) < mods(b);
      return a < b;
    };
    for (auto & seq : keymap_.sequencesFor(command, profile)) {
      // A raw control byte (a terminal's own spelling of a chord) reads as
      // nothing; the chord it stands for is bound too.
      bool printable = true;
      for (auto chord : seq) {
        int id = KeyChord::idOf(chord);
        if (id < 0x20 && id != Key::Tab && id != Key::Escape) printable = false;
      }
      if (printable && (best.empty() || simpler(seq, best))) best = seq;
    }
    return best.empty() ? std::string() : KeyChord::describe(best, profile);
  }

  // Whether this widget wants a bare Escape keypress delivered right away
  // (e.g. to close a popup), rather than only once the next key shows
  // whether it was the start of an Alt chord.
  virtual bool wantsBareEscape() const { return false; }

protected:

  bool dispatchCommand(const Input & input) {
    // RELEASE now reaches offerInput() (see Input::Kind's own doc
    // comment) - no keymap-bound command is meant to fire on key-up, so
    // ignore it here rather than have every widget's keymap guard against
    // it individually. REPEAT is untouched (still actionable, same as
    // before) - e.g. holding a transpose/kill-region binding should keep
    // repeating, unlike a widget's own raw note-entry keys, which
    // suppress repeat themselves.
    if (input.getKind() == Input::Kind::RELEASE) return false;
    auto chord = KeyChord::pack(input);

    // Two-key Emacs-style prefix sequences (C-x C-s, C-x C-c, ...) - a
    // pending prefix always consumes the very next keystroke one way or
    // another, matching Emacs's own "C-x <undefined-key>" behavior of
    // aborting the sequence rather than letting the second key fall
    // through to whatever it would otherwise have done (e.g. self-insert
    // in a text field) - a half-typed prefix should never silently do
    // something else instead.
    auto profile = keymapProfile();
    if (pending_prefix_) {
      auto prefix = *pending_prefix_;
      pending_prefix_.reset();
      if (auto name = keymap_.lookupPrefixed(prefix, chord, profile)) return commands_.execute(*name);
      return true;
    }
    if (keymap_.isPrefix(chord, profile)) {
      pending_prefix_ = chord;
      return true;
    }

    if (auto name = keymap_.lookup(chord, profile)) return commands_.execute(*name);
    return false;
  }

  // The profile the display uses; bindings are looked up in it.
  KeymapProfile keymapProfile() const { return plane_ ? plane_->keymapProfile() : KeymapProfile::EMACS; }

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

  // Lets a widget with planes of its own draw on each through the ordinary
  // drawing helpers: exchanges the plane the helpers draw on with `other`.
  void swapPlane(std::unique_ptr<UIPlane> & other) { plane_.swap(other); }

private:
  std::unique_ptr<UIPlane> plane_;
};

}  // namespace cellui
