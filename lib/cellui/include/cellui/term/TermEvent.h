#pragma once

#include <cellui/Input.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cellui::term {

// Modifier bits, in the kitty keyboard protocol's layout.
constexpr unsigned kModShift = 1;
constexpr unsigned kModAlt = 2;
constexpr unsigned kModCtrl = 4;
constexpr unsigned kModSuper = 8;
constexpr unsigned kModHyper = 16;
constexpr unsigned kModMeta = 32;
constexpr unsigned kModCapsLock = 64;
constexpr unsigned kModNumLock = 128;

// A reply to one of the start-up queries. Replies are recognised at any
// time; after start-up nothing needs them and they are dropped.
enum class ReplyKind {
  CURSOR_POSITION,  // params: row, col (1-based)
  PRIMARY_DA,       // params: the attributes
  SECONDARY_DA,     // params: Pp, Pv, Pc
  TERTIARY_DA,      // text: the unit id, in hex
  XTVERSION,        // text: name and version
  XTGETTCAP,        // text: the body after "1+r" or "0+r"; params[0]: 1 valid, 0 invalid
  MODE_REPORT,      // params: mode, state
  KITTY_KEYBOARD,   // params: flags
  XTSMGRAPHICS,     // params: Pi, Ps, values...
  KITTY_GRAPHICS,   // text: the body after "G"
  WINDOW_REPORT,    // params: 4 (text area px), 6 (cell px) or 8 (cells); height; width
  PALETTE_COLOR,    // params: index, r, g, b
  DEFAULT_COLOR,    // params: 10 (foreground) or 11 (background), r, g, b
  ITERM_CELL_SIZE,  // params: height, width (pixels)
};

// One thing read from the terminal.
struct TermEvent {
  enum class Type {
    KEY,
    MOUSE,
    PASTE,
    FOCUS_IN,
    FOCUS_OUT,
    RESIZE,      // rows, cols hold the new size
    END,         // the terminal is gone: end of input or hang-up
    UNKNOWN,     // a complete sequence nothing decodes; text holds its bytes
    REPLY,
  };
  // UNKNOWN for a terminal that reports no event types: a key press, or a
  // repeat that cannot be told from one, and never a release.
  enum class Kind { UNKNOWN, PRESS, REPEAT, RELEASE, DRAG, MOVE };

  Type type = Type::KEY;
  Kind kind = Kind::UNKNOWN;
  // A codepoint, or one of the NCKEY_* ids. An ASCII letter is reported in
  // upper case when Ctrl, Shift or Caps Lock is held (legacy terminals
  // cannot tell Ctrl-a from Ctrl-A).
  int key = 0;
  unsigned mods = 0;
  // The key on a US layout, when the terminal reports it (0 otherwise).
  int base_key = 0;
  // Mouse cell, 0-based; -1 for anything else.
  int y = -1, x = -1;
  // The text a key produced, a paste's text, or an unknown sequence's or
  // a reply's string.
  std::string text;
  // A paste larger than the cap arrives in several events; every one but
  // the last says so.
  bool continues = false;
  int rows = 0, cols = 0;
  ReplyKind reply = ReplyKind::CURSOR_POSITION;
  std::vector<int> params;

  bool operator==(const TermEvent & o) const {
    return type == o.type && kind == o.kind && key == o.key && mods == o.mods && base_key == o.base_key &&
           y == o.y && x == o.x && text == o.text && continues == o.continues && rows == o.rows &&
           cols == o.cols && (type != Type::REPLY || (reply == o.reply && params == o.params));
  }

  static TermEvent makeKey(int key, unsigned mods = 0, Kind kind = Kind::UNKNOWN) {
    TermEvent e;
    e.key = key;
    e.mods = mods;
    e.kind = kind;
    return e;
  }
};

// The application's Input for a key or mouse event: an upper-case letter
// becomes lower case with Shift (the original kept as the true-case id), as
// key bindings and note entry expect; Meta and Super both count as Meta.
Input toInput(const TermEvent & event);

// A readable form of an event, for tests and logs ("Ctrl+A", "Up release",
// "paste \"hi\"").
std::string describe(const TermEvent & event);

}  // namespace cellui::term
