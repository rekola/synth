#include <cellui/KeyChordNames.h>

#include <cellui/Utf8.h>

#include <cctype>

namespace cellui {
namespace KeyChord {

namespace {

std::string keyName(int id, bool emacs, bool upper) {
  switch (id) {
    case Key::Up: return "Up";
    case Key::Down: return "Down";
    case Key::Left: return "Left";
    case Key::Right: return "Right";
    case Key::Insert: return "Insert";
    case Key::Delete: return "Delete";
    case Key::Backspace: return emacs ? "DEL" : "Backspace";
    case Key::PageUp: return "PageUp";
    case Key::PageDown: return "PageDown";
    case Key::Home: return "Home";
    case Key::End: return "End";
    case Key::Enter: return emacs ? "RET" : "Enter";
    case Key::Tab: return emacs ? "TAB" : "Tab";
    case Key::Escape: return emacs ? "ESC" : "Esc";
    case Key::Space: return emacs ? "SPC" : "Space";
  }
  if (id >= Key::F1 && id <= Key::F1 + 11) return "F" + std::to_string(id - Key::F1 + 1);
  if (id >= 0x20 && id < Key::First) {
    if (id < 0x80 && std::isalpha(id) && upper) return std::string(1, static_cast<char>(std::toupper(id)));
    return utf8::encodeCodepoint(static_cast<uint32_t>(id));
  }
  return "?";
}

}  // namespace

std::string describe(const std::vector<uint64_t> & sequence, KeymapProfile profile) {
  bool emacs = profile == KeymapProfile::EMACS;
  std::string out;
  for (uint64_t chord : sequence) {
    if (!out.empty()) out += ' ';
    int id = idOf(chord);
    bool letter = id < 0x80 && std::isalpha(id);
    if (emacs) {
      if (hasCtrl(chord)) out += "C-";
      if (hasAlt(chord) || hasMeta(chord)) out += "M-";
      if (hasShift(chord)) out += "S-";
      out += keyName(id, true, hasShift(chord) && letter);
    } else {
      if (hasCtrl(chord)) out += "Ctrl+";
      if (hasAlt(chord)) out += "Alt+";
      if (hasShift(chord)) out += "Shift+";
      if (hasMeta(chord)) out += "Meta+";
      out += keyName(id, false, true);
    }
  }
  return out;
}

}  // namespace KeyChord
}  // namespace cellui
