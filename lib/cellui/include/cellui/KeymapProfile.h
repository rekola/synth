#pragma once

namespace cellui {

// Which family of key chords a display uses: the Emacs one (the terminal,
// where most Ctrl+Shift chords never arrive) or the usual desktop one.
enum class KeymapProfile { EMACS, DESKTOP };

}  // namespace cellui
