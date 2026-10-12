#pragma once

// pack(id, modifiers) lives in lib/cellui; the overload for the app's input
// event is here.
#include "../playback/InputEvent.h"

#include <cellui/KeyChord.h>

namespace KeyChord {

using cellui::KeyChord::pack;

inline uint64_t pack(const InputEvent & input) {
  return pack(input.getId(), input.hasCtrl(), input.hasAlt(), input.hasShift(), input.hasMeta());
}

}
