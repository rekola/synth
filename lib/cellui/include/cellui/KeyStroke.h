#pragma once

#include <cellui/Input.h>
#include <cellui/InputEvents.h>

#include <cstdint>

// A key id plus modifier state, packed into a single integer so it can be
// used directly as an unordered_map key (see Keymap.h) with no custom
// hash/equality specialization needed.
namespace cellui {
namespace KeyStroke {

inline uint64_t pack(int id, bool ctrl, bool alt, bool shift, bool meta) {
  return (static_cast<uint64_t>(id) << 4) |
    (ctrl ? 1u : 0u) | (alt ? 2u : 0u) | (shift ? 4u : 0u) | (meta ? 8u : 0u);
}

inline uint64_t pack(int id, const Modifiers & mods) {
  return pack(id, mods.ctrl, mods.alt, mods.shift, mods.meta);
}

inline uint64_t pack(const Input & input) {
  return pack(input.getId(), input.hasCtrl(), input.hasAlt(), input.hasShift(), input.hasMeta());
}

// The bits of pack(), for reading a shortcut back.
inline int idOf(uint64_t stroke) { return static_cast<int>(stroke >> 4); }
inline bool hasCtrl(uint64_t stroke) { return stroke & 1u; }
inline bool hasAlt(uint64_t stroke) { return stroke & 2u; }
inline bool hasShift(uint64_t stroke) { return stroke & 4u; }
inline bool hasMeta(uint64_t stroke) { return stroke & 8u; }

}  // namespace KeyStroke
}  // namespace cellui
