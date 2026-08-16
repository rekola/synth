#ifndef _KEYMAP_H_
#define _KEYMAP_H_

#include "CommandRegistry.h"

#include <cstdint>
#include <string>
#include <unordered_map>

// Maps key chords (see KeyChord.h) to command names, not directly to
// callables - this indirection is what lets a bound command also be invoked
// by name (e.g. from an M-x-style minibuffer), mirroring how Emacs keymaps
// bind keys to command symbols rather than to functions directly.
//
// Also supports two-key Emacs-style prefix sequences (C-x C-s, C-x C-c, ...)
// via bindPrefixed()/isPrefix()/lookupPrefixed() - a second, nested table
// rather than folding the prefix into the plain chord space, since a prefix
// chord (C-x) and a plain binding can coexist independently (nothing here
// stops some other widget from binding plain C-x to something outright) and
// because UIElement::dispatchCommand() needs to distinguish "this chord
// starts a sequence" from "this chord is itself a complete command" before
// it knows whether to wait for a second key at all.
class Keymap {
 public:
  void bind(uint64_t chord, std::string command_name) {
    bindings_[chord] = std::move(command_name);
  }

  const std::string * lookup(uint64_t chord) const {
    auto it = bindings_.find(chord);
    return it != bindings_.end() ? &it->second : nullptr;
  }

  // `prefix` (e.g. C-x) then `chord` (e.g. C-s) - see dispatchCommand()'s
  // own two-step state machine for how the prefix key is recognized and
  // held between keystrokes.
  void bindPrefixed(uint64_t prefix, uint64_t chord, std::string command_name) {
    prefixed_bindings_[prefix][chord] = std::move(command_name);
  }

  bool isPrefix(uint64_t chord) const {
    return prefixed_bindings_.count(chord) > 0;
  }

  const std::string * lookupPrefixed(uint64_t prefix, uint64_t chord) const {
    auto prefix_it = prefixed_bindings_.find(prefix);
    if (prefix_it == prefixed_bindings_.end()) return nullptr;
    auto it = prefix_it->second.find(chord);
    return it != prefix_it->second.end() ? &it->second : nullptr;
  }

  // Startup-time safety net: catch a typo'd command name (bound to a chord
  // but never registered in the paired registry) as an immediate assertion
  // failure instead of a silently dead keybinding.
  bool allBoundIn(const CommandRegistry & registry) const {
    for (auto & [chord, name] : bindings_) {
      if (!registry.has(name)) return false;
    }
    for (auto & [prefix, chords] : prefixed_bindings_) {
      for (auto & [chord, name] : chords) {
	if (!registry.has(name)) return false;
      }
    }
    return true;
  }

 private:
  std::unordered_map<uint64_t, std::string> bindings_;
  std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::string>> prefixed_bindings_;
};

#endif
