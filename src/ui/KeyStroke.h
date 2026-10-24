#pragma once

// The implementation lives in lib/cellui; this keeps the old name.
#include "../playback/InputEvent.h"

#include <cellui/KeyStroke.h>

namespace KeyStroke {

using cellui::KeyStroke::pack;

// An InputEvent is a cellui::Input, so the library overload serves it.

}
