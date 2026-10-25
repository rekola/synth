#pragma once

#include <cellui/CommandRegistry.h>
#include <cellui/KeymapProfile.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cellui {

// Maps key shortcuts (see KeyStroke.h) to command names, not directly to
// callables - this indirection is what lets a bound command also be invoked
// by name (e.g. from an command palette), mirroring how terminal keymaps
// bind keys to command symbols rather than to functions directly.
//
// Also supports two-key Emacs-style prefix sequences (C-x C-s, C-x C-c, ...)
// via bindPrefixed()/isPrefix()/lookupPrefixed() - a second, nested table
// rather than folding the prefix into the plain shortcut space, since a prefix
// shortcut (C-x) and a plain binding can coexist independently (nothing here
// stops some other widget from binding plain C-x to something outright) and
// because UIElement::dispatchCommand() needs to distinguish "this shortcut
// starts a sequence" from "this shortcut is itself a complete command" before
// it knows whether to wait for a second key at all.
class Keymap {
 public:
  // A binding made without a profile works in every profile; one made for a
  // profile only there, and wins over a shared binding of the same shortcut.
  void bind(uint64_t stroke, std::string command_name) {
    shared_.bindings[stroke] = std::move(command_name);
  }
  void bind(KeymapProfile profile, uint64_t stroke, std::string command_name) {
    table(profile).bindings[stroke] = std::move(command_name);
  }

  const std::string * lookup(uint64_t stroke, KeymapProfile profile) const {
    if (auto name = find(table(profile).bindings, stroke)) return name;
    return find(shared_.bindings, stroke);
  }

  void bindPrefixed(uint64_t prefix, uint64_t stroke, std::string command_name) {
    shared_.prefixed[prefix][stroke] = std::move(command_name);
  }
  void bindPrefixed(KeymapProfile profile, uint64_t prefix, uint64_t stroke, std::string command_name) {
    table(profile).prefixed[prefix][stroke] = std::move(command_name);
  }

  bool isPrefix(uint64_t stroke, KeymapProfile profile) const {
    return table(profile).prefixed.count(stroke) > 0 || shared_.prefixed.count(stroke) > 0;
  }

  // `prefix` (e.g. C-x) then `shortcut` (e.g. C-s), as dispatchCommand() tracks them.
  const std::string * lookupPrefixed(uint64_t prefix, uint64_t stroke, KeymapProfile profile) const {
    if (auto name = findPrefixed(table(profile), prefix, stroke)) return name;
    return findPrefixed(shared_, prefix, stroke);
  }

  // Startup-time safety net: a typo'd command name is an assertion failure
  // instead of a silently dead keybinding.
  // Every key sequence (one shortcut, or a prefix and a shortcut) that runs
  // `command_name` in `profile`, in no particular order.
  std::vector<std::vector<uint64_t>> sequencesFor(const std::string & command_name, KeymapProfile profile) const {
    std::vector<std::vector<uint64_t>> result;
    for (const Table * t : {&shared_, &table(profile)}) {
      for (auto & [stroke, name] : t->bindings) {
	if (name == command_name && lookup(stroke, profile) == &name) result.push_back({stroke});
      }
      for (auto & [prefix, strokes] : t->prefixed) {
	for (auto & [stroke, name] : strokes) {
	  if (name == command_name && lookupPrefixed(prefix, stroke, profile) == &name) result.push_back({prefix, stroke});
	}
      }
    }
    return result;
  }

  bool allBoundIn(const CommandRegistry & registry) const {
    return allBound(shared_, registry) && allBound(emacs_, registry) && allBound(desktop_, registry);
  }

 private:
  using Strokes = std::unordered_map<uint64_t, std::string>;
  struct Table {
    Strokes bindings;
    std::unordered_map<uint64_t, Strokes> prefixed;
  };

  Table & table(KeymapProfile profile) { return profile == KeymapProfile::TERMINAL ? emacs_ : desktop_; }
  const Table & table(KeymapProfile profile) const { return profile == KeymapProfile::TERMINAL ? emacs_ : desktop_; }

  static const std::string * find(const Strokes & strokes, uint64_t stroke) {
    auto it = strokes.find(stroke);
    return it != strokes.end() ? &it->second : nullptr;
  }
  static const std::string * findPrefixed(const Table & t, uint64_t prefix, uint64_t stroke) {
    auto it = t.prefixed.find(prefix);
    return it == t.prefixed.end() ? nullptr : find(it->second, stroke);
  }
  static bool allBound(const Table & t, const CommandRegistry & registry) {
    for (auto & [stroke, name] : t.bindings) {
      if (!registry.has(name)) return false;
    }
    for (auto & [prefix, strokes] : t.prefixed) {
      for (auto & [stroke, name] : strokes) {
	if (!registry.has(name)) return false;
      }
    }
    return true;
  }

  Table shared_, emacs_, desktop_;
};

}  // namespace cellui
