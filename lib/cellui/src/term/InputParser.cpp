#include <cellui/term/InputParser.h>

#include <cellui/Utf8.h>

#include <cmath>
#include <cstdlib>

namespace cellui::term {

namespace {

using Params = std::vector<std::vector<int>>;

constexpr char kPasteEnd[] = "\x1b[201~";
constexpr size_t kPasteEndLength = sizeof(kPasteEnd) - 1;
constexpr size_t kStringCap = 64 * 1024;

// Splits CSI parameter bytes into ';' fields of ':' sub-fields; an empty
// value is -1 (the default).
Params splitParams(const std::string & s) {
  Params out;
  if (s.empty()) return out;
  out.emplace_back();
  std::string number;
  auto flush = [&]() {
    out.back().push_back(number.empty() ? -1 : std::atoi(number.c_str()));
    number.clear();
  };
  for (char c : s) {
    if (c == ';') {
      flush();
      out.emplace_back();
    } else if (c == ':') {
      flush();
    } else if (c >= '0' && c <= '9') {
      if (number.size() < 9) number += c;
    }
  }
  flush();
  return out;
}

int field(const Params & p, size_t i, int def, size_t sub = 0) {
  if (i >= p.size() || sub >= p[i].size() || p[i][sub] < 0) return def;
  return p[i][sub];
}

std::vector<int> flatten(const Params & p) {
  std::vector<int> out;
  for (const auto & f : p) out.push_back(f.empty() ? -1 : f[0]);
  return out;
}

TermEvent::Kind eventKind(int code, bool kitty) {
  switch (code) {
    case 1: return TermEvent::Kind::PRESS;
    case 2: return TermEvent::Kind::REPEAT;
    case 3: return TermEvent::Kind::RELEASE;
    default: return kitty ? TermEvent::Kind::PRESS : TermEvent::Kind::UNKNOWN;
  }
}

// One colour component of an "rgb:r/g/b" reply: 1-4 hex digits, scaled to
// 8 bits by keeping the high byte.
int colorComponent(const std::string & hex) {
  if (hex.empty() || hex.size() > 4) return -1;
  int v = 0;
  for (char c : hex) {
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) return -1;
    v = v * 16 + d;
  }
  switch (hex.size()) {
    case 1: return v * 17;
    case 2: return v;
    case 3: return v >> 4;
    default: return v >> 8;
  }
}

bool parseRgb(const std::string & s, int & r, int & g, int & b) {
  if (s.compare(0, 4, "rgb:") != 0) return false;
  size_t a = 4, slash1 = s.find('/', a);
  if (slash1 == std::string::npos) return false;
  size_t slash2 = s.find('/', slash1 + 1);
  if (slash2 == std::string::npos) return false;
  r = colorComponent(s.substr(a, slash1 - a));
  g = colorComponent(s.substr(slash1 + 1, slash2 - slash1 - 1));
  b = colorComponent(s.substr(slash2 + 1));
  return r >= 0 && g >= 0 && b >= 0;
}

// Replaces malformed UTF-8 with U+FFFD. With `hold_tail`, an incomplete
// character at the end is not replaced but moved to `tail`.
std::string sanitizeUtf8(const std::string & s, bool hold_tail, std::string & tail) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t need = c < 0x80 ? 0 : c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : 99;
    if (need == 99) {
      out += "\xEF\xBF\xBD";
      i++;
      continue;
    }
    size_t k = 1;
    while (k <= need && i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80) k++;
    if (k == need + 1) {
      out.append(s, i, need + 1);
      i += need + 1;
    } else if (i + k == s.size() && hold_tail) {
      tail = s.substr(i);
      break;
    } else {
      out += "\xEF\xBF\xBD";
      i += k;
    }
  }
  return out;
}

int kittyFunctionalKey(int code) {
  if (code == 13) return Key::Enter;
  if (code == 9) return Key::Tab;
  if (code == 27) return Key::Escape;
  if (code == 127 || code == 8) return Key::Backspace;
  if (code >= 57358 && code <= 57363) return Key::CapsLock + (code - 57358);
  if (code >= 57376 && code <= 57398) return Key::F13 + (code - 57376);
  if (code >= 57399 && code <= 57408) return ('0' + (code - 57399));
  switch (code) {
    case 57409: return '.';
    case 57410: return '/';
    case 57411: return '*';
    case 57412: return '-';
    case 57413: return '+';
    case 57414: return Key::Enter;
    case 57415: return '=';
    case 57416: return Key::Separator;
    case 57417: return Key::Left;
    case 57418: return Key::Right;
    case 57419: return Key::Up;
    case 57420: return Key::Down;
    case 57421: return Key::PageUp;
    case 57422: return Key::PageDown;
    case 57423: return Key::Home;
    case 57424: return Key::End;
    case 57425: return Key::Insert;
    case 57426: return Key::Delete;
    case 57427: return Key::Begin;
    default: break;
  }
  if (code >= 57428 && code <= 57440) return Key::MediaPlay + (code - 57428);
  if (code >= 57441 && code <= 57452) return Key::LeftShift + (code - 57441);
  if (code == 57453) return Key::Level3Shift;
  if (code == 57454) return Key::Level5Shift;
  return (code);
}

// A keypad '/' or '*' with a modifier other than Shift is a key of its own.
int keypadOperator(int key, unsigned mods) {
  if ((mods & ~static_cast<unsigned>(kModShift | kModCapsLock | kModNumLock)) == 0) return key;
  if (key == '/') return Key::KpDivide;
  if (key == '*') return Key::KpMultiply;
  return key;
}

}  // namespace

InputParser::InputParser() = default;

TermEvent InputParser::pop() {
  TermEvent e = std::move(events_.front());
  events_.pop_front();
  return e;
}

std::deque<TermEvent> InputParser::takeAll() {
  std::deque<TermEvent> out;
  out.swap(events_);
  return out;
}

void InputParser::feed(const char * data, size_t size, TimePoint now) {
  if (ended_ || size == 0) return;
  for (size_t i = 0; i < size; i++) byte(static_cast<unsigned char>(data[i]));
  if (state_ == State::GROUND || state_ == State::PASTE) {
    deadline_.reset();
  } else {
    bool slow = state_ == State::STRING || state_ == State::STRING_ESC;
    deadline_ = now + (slow ? string_grace_ : grace_);
  }
}

void InputParser::expire(TimePoint now) {
  if (deadline_ && now >= *deadline_) {
    deadline_.reset();
    settle();
  }
}

void InputParser::finish() {
  if (ended_) return;
  settle();
  if (state_ == State::PASTE) {
    flushPaste(false);
    state_ = State::GROUND;
  }
  deadline_.reset();
  ended_ = true;
  TermEvent e;
  e.type = TermEvent::Type::END;
  emit(e);
}

void InputParser::reset() {
  state_ = State::GROUND;
  params_.clear();
  intermediates_.clear();
  string_.clear();
  utf8_.clear();
  raw_.clear();
  x10_.clear();
  alt_pending_ = false;
}

void InputParser::settle() {
  State was = state_;
  std::string held = string_;
  char intro = string_type_;
  bool bare = params_.empty() && intermediates_.empty();
  reset();
  switch (was) {
    case State::ESC:
      emitKey(Key::Escape, 0);
      break;
    case State::CSI:
      if (bare) emitKey('[', kModAlt);
      break;
    case State::SS3:
      if (bare) emitKey('O', kModAlt);
      break;
    case State::STRING_INTRO:
      emitKey((intro), kModAlt);
      for (char c : held) byte(static_cast<unsigned char>(c));
      if (state_ != State::GROUND) settle();
      break;
    case State::UTF8:
      emitKey(0xFFFD, 0);
      break;
    case State::PASTE:
      state_ = State::PASTE;
      break;
    default:
      break;
  }
}

void InputParser::emitKey(int key, unsigned mods, TermEvent::Kind kind, std::string text) {
  if (key >= 'a' && key <= 'z' && (mods & (kModCtrl | kModShift | kModCapsLock))) key -= 'a' - 'A';
  TermEvent e = TermEvent::makeKey(key, mods, kind);
  e.text = std::move(text);
  emit(std::move(e));
}

void InputParser::emitControl(unsigned char c, bool alt) {
  unsigned mods = alt ? kModAlt : 0;
  switch (c) {
    case 0x00: emitKey(' ', mods | kModCtrl); return;
    case 0x08:
    case 0x7F: emitKey(Key::Backspace, mods); return;
    case 0x09: emitKey(Key::Tab, mods); return;
    case 0x0A:
    case 0x0D: emitKey(Key::Enter, mods); return;
    case 0x1C: emitKey('\\', mods | kModCtrl); return;
    case 0x1D: emitKey(']', mods | kModCtrl); return;
    case 0x1E: emitKey('^', mods | kModCtrl); return;
    case 0x1F: emitKey('_', mods | kModCtrl); return;
    default: emitKey(('A' + c - 1), mods | kModCtrl); return;
  }
}

void InputParser::ground(unsigned char c, bool alt) {
  if (c < 0x20 || c == 0x7F) {
    emitControl(c, alt);
  } else if (c < 0x7F) {
    emitKey(c, alt ? kModAlt : 0, TermEvent::Kind::UNKNOWN, alt ? "" : std::string(1, static_cast<char>(c)));
  } else if (c >= 0xC2 && c <= 0xF4) {
    utf8_.assign(1, static_cast<char>(c));
    utf8_need_ = c <= 0xDF ? 1 : c <= 0xEF ? 2 : 3;
    alt_pending_ = alt;
    state_ = State::UTF8;
  } else {
    emitKey(0xFFFD, alt ? kModAlt : 0);
  }
}

bool InputParser::stringIntroPossible() const {
  const std::string & s = string_;
  if (string_type_ == ']') {
    for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == ';') return i > 0 && i + 1 == s.size();
      if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
  }
  const char * prefixes[4] = {">|", "!|", "1+r", "0+r"};
  if (string_type_ == '_') prefixes[0] = "G", prefixes[1] = prefixes[2] = prefixes[3] = nullptr;
  for (const char * p : prefixes) {
    if (p && std::string(p).compare(0, s.size(), s) == 0) return true;
  }
  return false;
}

void InputParser::byte(unsigned char c) {
  switch (state_) {
    case State::GROUND:
      if (c == 0x1B) {
        state_ = State::ESC;
        raw_.assign(1, '\x1b');
      } else {
        ground(c, false);
      }
      return;

    case State::ESC:
      raw_ += static_cast<char>(c);
      if (c == '[') {
        state_ = State::CSI;
        params_.clear();
        intermediates_.clear();
      } else if (c == 'O') {
        state_ = State::SS3;
        params_.clear();
      } else if (c == ']' || c == 'P' || c == '_') {
        state_ = State::STRING_INTRO;
        string_type_ = static_cast<char>(c);
        string_.clear();
      } else if (c == 0x1B) {
        emitKey(Key::Escape, 0);
        raw_.assign(1, '\x1b');
      } else {
        state_ = State::GROUND;
        ground(c, true);
      }
      return;

    case State::CSI:
      raw_ += static_cast<char>(c);
      if (c >= 0x30 && c <= 0x3F) {
        params_ += static_cast<char>(c);
      } else if (c >= 0x20 && c <= 0x2F) {
        intermediates_ += static_cast<char>(c);
      } else if (c >= 0x40 && c <= 0x7E) {
        state_ = State::GROUND;
        final_ = static_cast<char>(c);
        dispatchCsi();
        if (state_ == State::GROUND) reset();
      } else if (c == 0x1B) {
        reset();
        state_ = State::ESC;
        raw_.assign(1, '\x1b');
      } else {
        reset();
      }
      return;

    case State::SS3:
      raw_ += static_cast<char>(c);
      if (c >= '0' && c <= '9') {
        params_ += static_cast<char>(c);
      } else if (c >= 0x40 && c <= 0x7E) {
        state_ = State::GROUND;
        dispatchSs3(c);
        reset();
      } else if (c == 0x1B) {
        reset();
        state_ = State::ESC;
        raw_.assign(1, '\x1b');
      } else {
        reset();
      }
      return;

    case State::STRING_INTRO: {
      string_ += static_cast<char>(c);
      bool complete = string_type_ == ']' ? (string_.size() > 1 && string_.back() == ';')
                    : string_ == ">|" || string_ == "!|" || string_ == "1+r" || string_ == "0+r" || (string_type_ == '_' && string_ == "G");
      if (stringIntroPossible()) {
        if (complete) state_ = State::STRING;
        return;
      }
      // Not a reply after all: ESC and the introducer were Alt plus a key.
      std::string held = string_;
      char intro = string_type_;
      reset();
      emitKey((intro), kModAlt);
      for (char h : held) byte(static_cast<unsigned char>(h));
      return;
    }

    case State::STRING:
      if (c == 0x07) {
        dispatchString();
        reset();
      } else if (c == 0x1B) {
        state_ = State::STRING_ESC;
      } else if (string_.size() < kStringCap) {
        string_ += static_cast<char>(c);
      }
      return;

    case State::STRING_ESC:
      if (c == '\\') {
        dispatchString();
        reset();
      } else if (rxvt_) {
        dispatchString();
        reset();
        byte(c);
      } else {
        // ESC inside a string starts a new sequence and abandons the string.
        reset();
        state_ = State::ESC;
        raw_.assign(1, '\x1b');
        byte(c);
      }
      return;

    case State::UTF8:
      if ((c & 0xC0) == 0x80) {
        utf8_ += static_cast<char>(c);
        if (utf8_.size() == utf8_need_ + 1) {
          size_t i = 0;
          uint32_t cp = 0;
          unsigned char lead = static_cast<unsigned char>(utf8_[0]);
          cp = lead & (0x7Fu >> (utf8_need_ + 1));
          for (i = 1; i < utf8_.size(); i++) cp = (cp << 6) | (static_cast<unsigned char>(utf8_[i]) & 0x3Fu);
          bool overlong = (utf8_need_ == 2 && cp < 0x800) || (utf8_need_ == 3 && cp < 0x10000);
          bool alt = alt_pending_;
          std::string text = utf8_;
          reset();
          if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) emitKey(0xFFFD, alt ? kModAlt : 0);
          else emitKey(static_cast<int>(cp), alt ? kModAlt : 0, TermEvent::Kind::UNKNOWN, alt ? "" : text);
        }
      } else {
        bool alt = alt_pending_;
        reset();
        emitKey(0xFFFD, alt ? kModAlt : 0);
        byte(c);
      }
      return;

    case State::PASTE:
      pasteByte(c);
      return;

    case State::X10:
      x10_ += static_cast<char>(c);
      if (x10_.size() == 3) {
        dispatchX10Mouse();
        reset();
      }
      return;
  }
}

unsigned InputParser::legacyMods(int param) const {
  if (param < 1) return 0;
  unsigned bits = static_cast<unsigned>(param - 1);
  if (kitty_) return bits;
  unsigned mods = bits & (kModShift | kModAlt | kModCtrl);
  if (bits & 8) mods |= kModMeta;
  return mods;
}

void InputParser::dispatchCsi() {
  char prefix = 0;
  std::string body = params_;
  if (!body.empty() && (body[0] == '<' || body[0] == '=' || body[0] == '>' || body[0] == '?')) {
    prefix = body[0];
    body.erase(0, 1);
  }
  Params params = splitParams(body);
  auto reply = [&](ReplyKind kind) {
    TermEvent e;
    e.type = TermEvent::Type::REPLY;
    e.reply = kind;
    e.params = flatten(params);
    emit(std::move(e));
  };
  auto unknown = [&]() {
    TermEvent e;
    e.type = TermEvent::Type::UNKNOWN;
    e.text = raw_;
    emit(std::move(e));
  };

  if (prefix == '?') {
    if (final_ == 'c' && intermediates_.empty()) reply(ReplyKind::PRIMARY_DA);
    else if (final_ == 'y' && intermediates_ == "$") reply(ReplyKind::MODE_REPORT);
    else if (final_ == 'u' && intermediates_.empty()) reply(ReplyKind::KITTY_KEYBOARD);
    else if (final_ == 'S' && intermediates_.empty()) reply(ReplyKind::XTSMGRAPHICS);
    else unknown();
    return;
  }
  if (prefix == '>') {
    if (final_ == 'c' && intermediates_.empty()) reply(ReplyKind::SECONDARY_DA);
    else unknown();
    return;
  }
  if (prefix == '<') {
    if ((final_ == 'M' || final_ == 'm') && intermediates_.empty() && params.size() >= 3) {
      dispatchSgrMouse(params, final_ == 'm');
    } else {
      unknown();
    }
    return;
  }
  if (prefix || !intermediates_.empty()) {
    unknown();
    return;
  }
  switch (final_) {
    case 'R':
      if (cursor_reports_expected_ > 0 && params.size() == 2) {
        cursor_reports_expected_--;
        reply(ReplyKind::CURSOR_POSITION);
      } else {
        dispatchLetterKey(final_, params);
      }
      return;
    case 't':
      if (params.size() == 3 && (field(params, 0, 0) == 4 || field(params, 0, 0) == 6 || field(params, 0, 0) == 8)) {
        reply(ReplyKind::WINDOW_REPORT);
      } else {
        unknown();
      }
      return;
    case 'u':
      if (!dispatchKittyKey(params)) unknown();
      return;
    case '~':
      dispatchTildeKey(params);
      return;
    case 'I':
    case 'O':
      if (params.empty()) {
        TermEvent e;
        e.type = final_ == 'I' ? TermEvent::Type::FOCUS_IN : TermEvent::Type::FOCUS_OUT;
        emit(std::move(e));
      } else {
        unknown();
      }
      return;
    case 'M':
      if (params.empty()) {
        state_ = State::X10;
        x10_.clear();
        return;
      }
      unknown();
      return;
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'H':
    case 'P': case 'Q': case 'S': case 'Z':
      dispatchLetterKey(final_, params);
      return;
    default:
      unknown();
      return;
  }
}

void InputParser::dispatchLetterKey(char final_byte, const Params & params) {
  int key = 0;
  unsigned extra = 0;
  switch (final_byte) {
    case 'A': key = Key::Up; break;
    case 'B': key = Key::Down; break;
    case 'C': key = Key::Right; break;
    case 'D': key = Key::Left; break;
    case 'E': key = Key::Begin; break;
    case 'F': key = Key::End; break;
    case 'H': key = Key::Home; break;
    case 'P': key = Key::F1; break;
    case 'Q': key = Key::F2; break;
    case 'R': key = Key::F3; break;
    case 'S': key = Key::F4; break;
    case 'Z': key = Key::Tab; extra = kModShift; break;
    default: return;
  }
  int mod = 1, event = -1;
  if (params.size() >= 2) {
    mod = field(params, 1, 1);
    event = field(params, 1, -1, 1);
  } else if (params.size() == 1) {
    // An old xterm form puts the modifier in the only parameter.
    mod = field(params, 0, 1);
  }
  emitKey(key, legacyMods(mod) | extra, eventKind(event, kitty_));
}

void InputParser::dispatchTildeKey(const Params & params) {
  int number = field(params, 0, -1);
  int mod = field(params, 1, 1);
  int event = field(params, 1, -1, 1);
  int key = 0;
  switch (number) {
    case 1: case 7: key = Key::Home; break;
    case 2: key = Key::Insert; break;
    case 3: key = Key::Delete; break;
    case 4: case 8: key = Key::End; break;
    case 5: key = Key::PageUp; break;
    case 6: key = Key::PageDown; break;
    case 11: case 12: case 13: case 14: case 15:
      key = Key::F1 + (number - 11);
      break;
    case 17: case 18: case 19: case 20: case 21:
      key = Key::F6 + (number - 17);
      break;
    case 23: key = Key::F11; break;
    case 24: key = Key::F12; break;
    case 25: key = Key::F13; break;
    case 26: key = Key::F14; break;
    case 28: key = Key::F15; break;
    case 29: key = Key::F16; break;
    case 31: case 32: case 33: case 34:
      key = Key::F17 + (number - 31);
      break;
    case 27: {
      // modifyOtherKeys: CSI 27 ; modifier ; code ~
      int code = field(params, 2, -1);
      if (code < 0) break;
      int k = code == 13 ? Key::Enter : code == 9 ? Key::Tab : code == 27 ? Key::Escape
                 : code == 127 || code == 8 ? Key::Backspace : (code);
      emitKey(k, legacyMods(mod), eventKind(-1, kitty_));
      return;
    }
    case 200:
      state_ = State::PASTE;
      paste_.clear();
      paste_match_ = 0;
      paste_last_cr_ = false;
      return;
    case 201:
      return;
    default:
      break;
  }
  if (!key) {
    TermEvent e;
    e.type = TermEvent::Type::UNKNOWN;
    e.text = raw_;
    emit(std::move(e));
    return;
  }
  emitKey(key, legacyMods(mod), eventKind(event, kitty_));
}

bool InputParser::dispatchKittyKey(const Params & params) {
  int code = field(params, 0, -1);
  if (code < 0) return false;
  int base = field(params, 0, 0, 2);
  int mod = field(params, 1, 1);
  int event = field(params, 1, 1, 1);
  unsigned mods = mod >= 1 ? static_cast<unsigned>(mod - 1) : 0;
  int key = kittyFunctionalKey(code);
  if (code == 57410 || code == 57411) key = keypadOperator(key, mods);
  std::string text;
  if (params.size() >= 3) {
    for (int cp : params[2]) {
      if (cp > 0) text += utf8::encodeCodepoint(static_cast<uint32_t>(cp));
    }
  }
  if (key >= 'a' && key <= 'z' && (mods & (kModCtrl | kModShift | kModCapsLock))) key -= 'a' - 'A';
  TermEvent e = TermEvent::makeKey(key, mods, eventKind(event, true));
  e.base_key = base > 0 ? base : 0;
  e.text = std::move(text);
  emit(std::move(e));
  return true;
}

void InputParser::dispatchSs3(unsigned char final_byte) {
  unsigned mods = params_.empty() ? 0 : legacyMods(std::atoi(params_.c_str()));
  TermEvent::Kind kind = eventKind(-1, kitty_);
  int key = 0;
  switch (final_byte) {
    case 'A': key = Key::Up; break;
    case 'B': key = Key::Down; break;
    case 'C': key = Key::Right; break;
    case 'D': key = Key::Left; break;
    case 'E': key = Key::Begin; break;
    case 'F': key = Key::End; break;
    case 'H': key = Key::Home; break;
    case 'P': key = Key::F1; break;
    case 'Q': key = Key::F2; break;
    case 'R': key = Key::F3; break;
    case 'S': key = Key::F4; break;
    case 'M': key = Key::Enter; break;
    case 'X': key = '='; break;
    case 'j': key = '*'; break;
    case 'k': key = '+'; break;
    case 'l': key = ','; break;
    case 'm': key = '-'; break;
    case 'n': key = '.'; break;
    case 'o': key = '/'; break;
    default:
      if (final_byte >= 'p' && final_byte <= 'y') key = ('0' + (final_byte - 'p'));
      break;
  }
  if (!key) {
    TermEvent e;
    e.type = TermEvent::Type::UNKNOWN;
    e.text = raw_;
    emit(std::move(e));
    return;
  }
  emitKey(keypadOperator(key, mods), mods, kind);
}

void InputParser::dispatchString() {
  TermEvent e;
  e.type = TermEvent::Type::REPLY;
  const std::string & s = string_;
  if (string_type_ == ']') {
    size_t semi = s.find(';');
    if (semi == std::string::npos) return;
    int number = std::atoi(s.substr(0, semi).c_str());
    std::string rest = s.substr(semi + 1);
    int r, g, b;
    if (number == 4) {
      size_t semi2 = rest.find(';');
      if (semi2 == std::string::npos || !parseRgb(rest.substr(semi2 + 1), r, g, b)) return;
      e.reply = ReplyKind::PALETTE_COLOR;
      e.params = {std::atoi(rest.substr(0, semi2).c_str()), r, g, b};
    } else if (number == 10 || number == 11) {
      if (!parseRgb(rest, r, g, b)) return;
      e.reply = ReplyKind::DEFAULT_COLOR;
      e.params = {number, r, g, b};
    } else if (number == 1337 && rest.compare(0, 15, "ReportCellSize=") == 0) {
      std::string v = rest.substr(15);
      size_t semi2 = v.find(';');
      if (semi2 == std::string::npos) return;
      e.reply = ReplyKind::ITERM_CELL_SIZE;
      e.params = {static_cast<int>(std::lround(std::atof(v.substr(0, semi2).c_str()))),
                  static_cast<int>(std::lround(std::atof(v.substr(semi2 + 1).c_str())))};
    } else {
      return;
    }
  } else if (string_type_ == 'P') {
    if (s.compare(0, 2, ">|") == 0) {
      e.reply = ReplyKind::XTVERSION;
      e.text = s.substr(2);
    } else if (s.compare(0, 2, "!|") == 0) {
      e.reply = ReplyKind::TERTIARY_DA;
      e.text = s.substr(2);
    } else if (s.compare(1, 2, "+r") == 0) {
      e.reply = ReplyKind::XTGETTCAP;
      e.params = {s[0] == '1' ? 1 : 0};
      e.text = s.substr(3);
    } else {
      return;
    }
  } else if (string_type_ == '_' && !s.empty() && s[0] == 'G') {
    e.reply = ReplyKind::KITTY_GRAPHICS;
    e.text = s.substr(1);
  } else {
    return;
  }
  emit(std::move(e));
}

void InputParser::dispatchSgrMouse(const Params & params, bool release) {
  mouse(field(params, 0, 0), field(params, 1, 1), field(params, 2, 1), release, pixel_mouse_);
}

void InputParser::dispatchX10Mouse() {
  int code = static_cast<unsigned char>(x10_[0]) - 32;
  int px = static_cast<unsigned char>(x10_[1]) - 32;
  int py = static_cast<unsigned char>(x10_[2]) - 32;
  // X10 says "a button was released" without saying which.
  bool release = (code & 3) == 3 && !(code & (32 | 64 | 128));
  if (release) code &= ~3;
  mouse(code, px, py, release, false);
}

void InputParser::mouse(int code, int px, int py, bool release, bool pixels) {
  int y, x;
  float fy, fx;
  if (pixels) {
    if (cell_px_h_ <= 0 || cell_px_w_ <= 0) return;
    y = (py - 1) / cell_px_h_;
    x = (px - 1) / cell_px_w_;
    // The middle of the reported pixel.
    fy = (static_cast<float>(py) - 0.5f) / static_cast<float>(cell_px_h_);
    fx = (static_cast<float>(px) - 0.5f) / static_cast<float>(cell_px_w_);
  } else {
    y = py - 1;
    x = px - 1;
    fy = static_cast<float>(y);
    fx = static_cast<float>(x);
  }
  if (y < 0 || x < 0) return;
  if (screen_rows_ > 0 && y >= screen_rows_) return;
  if (screen_cols_ > 0 && x >= screen_cols_) return;
  unsigned mods = 0;
  if (code & 4) mods |= kModShift;
  if (code & 8) mods |= kModAlt;
  if (code & 16) mods |= kModCtrl;
  int low = code & 3;
  int key;
  if (code & 64) key = Key::Button4 + (low);
  else if (code & 128) key = Key::Button8 + (low);
  else if (low == 3) key = Key::Motion;
  else key = Key::Button1 + (low);
  TermEvent::Kind kind;
  if (code & 32) kind = key == Key::Motion ? TermEvent::Kind::MOVE : TermEvent::Kind::DRAG;
  else kind = release ? TermEvent::Kind::RELEASE : TermEvent::Kind::PRESS;
  TermEvent e = TermEvent::makeKey(key, mods, kind);
  e.type = TermEvent::Type::MOUSE;
  e.y = y;
  e.x = x;
  e.fy = fy;
  e.fx = fx;
  emit(std::move(e));
}

void InputParser::pasteByte(unsigned char c) {
  if (static_cast<char>(c) == kPasteEnd[paste_match_]) {
    if (++paste_match_ == kPasteEndLength) {
      paste_match_ = 0;
      flushPaste(false);
      reset();
    }
    return;
  }
  auto append = [this](char ch) {
    if (ch == '\r') {
      paste_ += '\n';
      paste_last_cr_ = true;
      return;
    }
    if (ch == '\n' && paste_last_cr_) {
      paste_last_cr_ = false;
      return;
    }
    paste_last_cr_ = false;
    paste_ += ch;
    if (paste_.size() >= paste_cap_) flushPaste(true);
  };
  if (paste_match_ > 0) {
    std::string seen(kPasteEnd, paste_match_);
    paste_match_ = 0;
    for (char ch : seen) append(ch);
    if (static_cast<char>(c) == kPasteEnd[0]) {
      paste_match_ = 1;
      return;
    }
  }
  append(static_cast<char>(c));
}

void InputParser::flushPaste(bool continues) {
  std::string tail;
  TermEvent e;
  e.type = TermEvent::Type::PASTE;
  e.text = sanitizeUtf8(paste_, continues, tail);
  e.continues = continues;
  paste_ = tail;
  if (continues && e.text.empty()) return;
  emit(std::move(e));
}

}  // namespace cellui::term
