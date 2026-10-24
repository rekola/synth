#pragma once

namespace cellui {

// Which family of key shortcuts a display uses: the Emacs one (the terminal,
// where most Ctrl+Shift shortcuts never arrive) or the usual desktop one.
enum class KeymapProfile { TERMINAL, DESKTOP };

}  // namespace cellui
