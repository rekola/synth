#pragma once

#include <cellui/CommandRegistry.h>
#include <cellui/KeymapProfile.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cellui {

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
  // A binding made without a profile works in every profile; one made for a
  // profile only there, and wins over a shared binding of the same chord.
  void bind(uint64_t chord, std::string command_name) {
    shared_.bindings[chord] = std::move(command_name);
  }
  void bind(KeymapProfile profile, uint64_t chord, std::string command_name) {
    table(profile).bindings[chord] = std::move(command_name);
  }

  const std::string * lookup(uint64_t chord, KeymapProfile profile) const {
    if (auto name = find(table(profile).bindings, chord)) return name;
    return find(shared_.bindings, chord);
  }

  void bindPrefixed(uint64_t prefix, uint64_t chord, std::string command_name) {
    shared_.prefixed[prefix][chord] = std::move(command_name);
  }
  void bindPrefixed(KeymapProfile profile, uint64_t prefix, uint64_t chord, std::string command_name) {
    table(profile).prefixed[prefix][chord] = std::move(command_name);
  }

  bool isPrefix(uint64_t chord, KeymapProfile profile) const {
    return table(profile).prefixed.count(chord) > 0 || shared_.prefixed.count(chord) > 0;
  }

  // `prefix` (e.g. C-x) then `chord` (e.g. C-s), as dispatchCommand() tracks them.
  const std::string * lookupPrefixed(uint64_t prefix, uint64_t chord, KeymapProfile profile) const {
    if (auto name = findPrefixed(table(profile), prefix, chord)) return name;
    return findPrefixed(shared_, prefix, chord);
  }

  // Startup-time safety net: a typo'd command name is an assertion failure
  // instead of a silently dead keybinding.
  // Every key sequence (one chord, or a prefix and a chord) that runs
  // `command_name` in `profile`, in no particular order.
  std::vector<std::vector<uint64_t>> sequencesFor(const std::string & command_name, KeymapProfile profile) const {
    std::vector<std::vector<uint64_t>> result;
    for (const Table * t : {&shared_, &table(profile)}) {
      for (auto & [chord, name] : t->bindings) {
	if (name == command_name && lookup(chord, profile) == &name) result.push_back({chord});
      }
      for (auto & [prefix, chords] : t->prefixed) {
	for (auto & [chord, name] : chords) {
	  if (name == command_name && lookupPrefixed(prefix, chord, profile) == &name) result.push_back({prefix, chord});
	}
      }
    }
    return result;
  }

  bool allBoundIn(const CommandRegistry & registry) const {
    return allBound(shared_, registry) && allBound(emacs_, registry) && allBound(desktop_, registry);
  }

 private:
  using Chords = std::unordered_map<uint64_t, std::string>;
  struct Table {
    Chords bindings;
    std::unordered_map<uint64_t, Chords> prefixed;
  };

  Table & table(KeymapProfile profile) { return profile == KeymapProfile::EMACS ? emacs_ : desktop_; }
  const Table & table(KeymapProfile profile) const { return profile == KeymapProfile::EMACS ? emacs_ : desktop_; }

  static const std::string * find(const Chords & chords, uint64_t chord) {
    auto it = chords.find(chord);
    return it != chords.end() ? &it->second : nullptr;
  }
  static const std::string * findPrefixed(const Table & t, uint64_t prefix, uint64_t chord) {
    auto it = t.prefixed.find(prefix);
    return it == t.prefixed.end() ? nullptr : find(it->second, chord);
  }
  static bool allBound(const Table & t, const CommandRegistry & registry) {
    for (auto & [chord, name] : t.bindings) {
      if (!registry.has(name)) return false;
    }
    for (auto & [prefix, chords] : t.prefixed) {
      for (auto & [chord, name] : chords) {
	if (!registry.has(name)) return false;
      }
    }
    return true;
  }

  Table shared_, emacs_, desktop_;
};

}  // namespace cellui
