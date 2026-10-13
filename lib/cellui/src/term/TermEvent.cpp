#include <cellui/term/TermEvent.h>

#include <cellui/Utf8.h>

namespace cellui::term {

namespace {

// The single codepoint `text` holds, or 0.
uint32_t singleCodepoint(const std::string & text) {
  if (text.empty()) return 0;
  unsigned char c = static_cast<unsigned char>(text[0]);
  size_t need = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
  if (text.size() != need + 1) return 0;
  uint32_t cp = need == 0 ? c : c & (0x3Fu >> need);
  for (size_t i = 1; i < text.size(); i++) cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 0x3Fu);
  return cp;
}

const char * keyName(int key) {
  switch (key) {
    case Key::Up: return "Up";
    case Key::Down: return "Down";
    case Key::Left: return "Left";
    case Key::Right: return "Right";
    case Key::Home: return "Home";
    case Key::End: return "End";
    case Key::Begin: return "Begin";
    case Key::Insert: return "Insert";
    case Key::Delete: return "Delete";
    case Key::PageUp: return "PgUp";
    case Key::PageDown: return "PgDn";
    case Key::Backspace: return "Backspace";
    case Key::Enter: return "Enter";
    case Key::Tab: return "Tab";
    case Key::Escape: return "Esc";
    case Key::Space: return "Space";
    case Key::Motion: return "Motion";
    case Key::Button4: return "WheelUp";
    case Key::Button5: return "WheelDown";
    case Key::Button6: return "WheelLeft";
    case Key::Button7: return "WheelRight";
    case Key::LeftShift: return "LeftShift";
    case Key::KpDivide: return "KpDivide";
    case Key::KpMultiply: return "KpMultiply";
    case Key::Separator: return "KpSeparator";
    default: return nullptr;
  }
}

std::string keyText(int key) {
  if (const char * name = keyName(key)) return name;
  if (key >= Key::F0 && key <= Key::F60) return "F" + std::to_string(key - Key::F0);
  if (key >= Key::Button1 && key <= Key::Button11) return "Button" + std::to_string(key - Key::Button1 + 1);
  if (key >= Key::First) return "Key" + std::to_string(key - Key::First);
  return utf8::encodeCodepoint(static_cast<uint32_t>(key));
}

}  // namespace

Input toInput(const TermEvent & e) {
  bool alt = e.mods & kModAlt;
  bool shift = e.mods & kModShift;
  bool ctrl = e.mods & kModCtrl;
  bool meta = e.mods & (kModMeta | kModSuper);
  Input::Kind kind;
  switch (e.kind) {
    case TermEvent::Kind::PRESS:
    case TermEvent::Kind::DRAG: kind = Input::Kind::PRESS; break;
    case TermEvent::Kind::REPEAT: kind = Input::Kind::REPEAT; break;
    case TermEvent::Kind::RELEASE:
    case TermEvent::Kind::MOVE: kind = Input::Kind::RELEASE; break;
    default: kind = Input::Kind::UNKNOWN; break;
  }
  if (e.type == TermEvent::Type::MOUSE) {
    return Input(e.key, e.y, e.x, alt, shift, ctrl, meta, kind);
  }
  if (e.type == TermEvent::Type::RESIZE) return Input(Key::Resize, -1, -1, false, false, false, false);
  if (e.type == TermEvent::Type::END) return Input(Key::Eof, -1, -1, false, false, false, false);
  if (e.type != TermEvent::Type::KEY) return Input(Key::Invalid, -1, -1, false, false, false, false);

  // A key whose produced character the terminal reported ('!' for Shift+1)
  // is that character, as a legacy terminal sends it.
  int raw = e.key;
  uint32_t produced = singleCodepoint(e.text);
  if (produced >= 0x20 && produced != 0x7F && !ctrl && !alt) raw = static_cast<int>(produced);
  int id = raw;
  if (id >= 'A' && id <= 'Z') {
    id += 'a' - 'A';
    if (!ctrl) shift = true;
  }
  return Input(id, -1, -1, alt, shift, ctrl, meta, kind, raw);
}

std::string describe(const TermEvent & e) {
  std::string mods;
  if (e.mods & kModCtrl) mods += "Ctrl+";
  if (e.mods & kModAlt) mods += "Alt+";
  if (e.mods & kModShift) mods += "Shift+";
  if (e.mods & kModSuper) mods += "Super+";
  if (e.mods & kModMeta) mods += "Meta+";
  std::string kind;
  switch (e.kind) {
    case TermEvent::Kind::PRESS: kind = " press"; break;
    case TermEvent::Kind::REPEAT: kind = " repeat"; break;
    case TermEvent::Kind::RELEASE: kind = " release"; break;
    case TermEvent::Kind::DRAG: kind = " drag"; break;
    case TermEvent::Kind::MOVE: kind = " move"; break;
    default: break;
  }
  switch (e.type) {
    case TermEvent::Type::KEY: {
      std::string s = mods + keyText(e.key) + kind;
      if (!e.text.empty() && e.text != keyText(e.key)) s += " text \"" + e.text + "\"";
      return s;
    }
    case TermEvent::Type::MOUSE:
      return mods + keyText(e.key) + kind + " at " + std::to_string(e.y) + "," + std::to_string(e.x);
    case TermEvent::Type::PASTE: return std::string("paste \"") + e.text + "\"" + (e.continues ? " continues" : "");
    case TermEvent::Type::FOCUS_IN: return "focus in";
    case TermEvent::Type::FOCUS_OUT: return "focus out";
    case TermEvent::Type::RESIZE: return "resize " + std::to_string(e.rows) + "x" + std::to_string(e.cols);
    case TermEvent::Type::END: return "end";
    case TermEvent::Type::UNKNOWN: return "unknown";
    case TermEvent::Type::REPLY: return "reply";
  }
  return "";
}

}  // namespace cellui::term
