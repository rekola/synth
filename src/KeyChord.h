#ifndef _KEYCHORD_H_
#define _KEYCHORD_H_

#include "InputEvent.h"

#include <cstdint>

// A key id plus modifier state, packed into a single integer so it can be
// used directly as an unordered_map key (see Keymap.h) with no custom
// hash/equality specialization needed.
namespace KeyChord {

inline uint64_t pack(int id, bool ctrl, bool alt, bool shift, bool meta) {
  return (static_cast<uint64_t>(id) << 4) |
    (ctrl ? 1u : 0u) | (alt ? 2u : 0u) | (shift ? 4u : 0u) | (meta ? 8u : 0u);
}

inline uint64_t pack(const InputEvent & input) {
  return pack(input.getId(), input.hasCtrl(), input.hasAlt(), input.hasShift(), input.hasMeta());
}

}

#endif
