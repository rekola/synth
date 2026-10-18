#pragma once

#include <cellui/KeyChord.h>
#include <cellui/KeymapProfile.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cellui {
namespace KeyChord {

// A key sequence as a person reads it: "C-x C-s", "M-w", "C-S-Up" on the
// Emacs keymap, "Ctrl+S", "Ctrl+Shift+Z" on the desktop one.
std::string describe(const std::vector<uint64_t> & sequence, KeymapProfile profile);

}  // namespace KeyChord
}  // namespace cellui
