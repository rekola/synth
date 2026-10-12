#pragma once

// The implementation lives in lib/cellui; this keeps the old name.
#include "../playback/InputEvent.h"

#include <cellui/KeyChord.h>

namespace KeyChord {

using cellui::KeyChord::pack;

// An InputEvent is a cellui::Input, so the library overload serves it.

}
