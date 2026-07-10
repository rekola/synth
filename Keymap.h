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
class Keymap {
 public:
  void bind(uint64_t chord, std::string command_name) {
    bindings_[chord] = std::move(command_name);
  }

  const std::string * lookup(uint64_t chord) const {
    auto it = bindings_.find(chord);
    return it != bindings_.end() ? &it->second : nullptr;
  }

  // Startup-time safety net: catch a typo'd command name (bound to a chord
  // but never registered in the paired registry) as an immediate assertion
  // failure instead of a silently dead keybinding.
  bool allBoundIn(const CommandRegistry & registry) const {
    for (auto & [chord, name] : bindings_) {
      if (!registry.has(name)) return false;
    }
    return true;
  }

 private:
  std::unordered_map<uint64_t, std::string> bindings_;
};

#endif
