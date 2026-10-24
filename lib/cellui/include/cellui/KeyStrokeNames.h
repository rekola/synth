#pragma once

#include <cellui/KeyStroke.h>
#include <cellui/KeymapProfile.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cellui {
namespace KeyStroke {

// A key sequence as a person reads it: "C-x C-s", "M-w", "C-S-Up" on the
// terminal keymap, "Ctrl+S", "Ctrl+Shift+Z" on the desktop one.
std::string describe(const std::vector<uint64_t> & sequence, KeymapProfile profile);

}  // namespace KeyStroke
}  // namespace cellui
