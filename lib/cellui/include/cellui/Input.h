#pragma once

#ifndef PRETERUNICODEBASE

#define PRETERUNICODEBASE 1115000

#define preterunicode(w) ((w) + PRETERUNICODEBASE)  

#define NCKEY_TAB      0x09
#define NCKEY_ESC      0x1b
#define NCKEY_SPACE    0x20

// Special composed key definitions. These values are added to 0x100000.                 
#define NCKEY_INVALID preterunicode(0)                                                      
#define NCKEY_RESIZE  preterunicode(1) // generated internally in response to SIGWINCH      
#define NCKEY_UP      preterunicode(2)                                                      
#define NCKEY_RIGHT   preterunicode(3)                                                      
#define NCKEY_DOWN    preterunicode(4)                                                      
#define NCKEY_LEFT    preterunicode(5)

#define NCKEY_INS       preterunicode(6)
#define NCKEY_DEL   preterunicode(7)
#define NCKEY_BACKSPACE   preterunicode(8)
#define NCKEY_PGDOWN  preterunicode(9)
#define NCKEY_PGUP    preterunicode(10)

#define NCKEY_ENTER   preterunicode(121)

#define NCKEY_F02     preterunicode(22)

#define NCKEY_BUTTON1  preterunicode(201)
#define NCKEY_BUTTON2  preterunicode(202)
#define NCKEY_BUTTON3  preterunicode(203)
#define NCKEY_BUTTON4  preterunicode(204) // scrollwheel up
#define NCKEY_BUTTON5  preterunicode(205) // scrollwheel down

#endif

// Synthetic, non-notcurses key ids: no real Ctrl-modified numeric-keypad
// Divide/Multiply key exists in notcurses's own enum, nor does notcurses
// recognize the legacy VT220/xterm "SS3 + modifier digit" encoding some
// terminals still send for them (ESC 'O' '5' 'o'/'j') as a single key
// event at all - see TerminalUI::readInput()'s own escape-sequence
// recognizer, which synthesizes InputEvents carrying these ids instead of
// leaking the 4 raw bytes through as if they'd been typed. Defined
// unconditionally (unlike the block above) since they have no notcurses
// equivalent to fall back to either way, and picked well above any index
// notcurses's own nckeys.h uses (currently tops out at 500), so they can
// never collide with a real key.
#define NCKEY_KP_DIVIDE   preterunicode(900)
#define NCKEY_KP_MULTIPLY preterunicode(901)

namespace cellui {

class Input {
 public:
  // Kitty-protocol terminals (kitty, foot, wezterm, ghostty, ...) report
  // real press/repeat/release for each physical key - see
  // TerminalUI::readInput(). A terminal with no such support (notcurses
  // reports NCTYPE_UNKNOWN for literally every keystroke there, its own
  // signal that the terminal never negotiated the protocol at all - no
  // capability query needed) can't distinguish a fresh press from a held
  // key's auto-repeat or its eventual release, so UNKNOWN is kept
  // distinct from PRESS rather than folded into it: code that tracks
  // "this key is currently held" (note-entry's active_keyboard_notes_,
  // the realtime-auto-play-while-held feature) must treat UNKNOWN as "no
  // hold information available" and fall back to the old one-shot-per-
  // keystroke behavior, not silently mistake it for a real, eventually-
  // released press - see PatternEditor::offerInput()'s own note-entry
  // code for exactly where that fallback happens.
  enum class Kind { UNKNOWN, PRESS, REPEAT, RELEASE };

  // true_case_id defaults to id itself - only TerminalUI::readInput()'s
  // own dispatchRawKey ever passes a distinct value, for a plain letter
  // key: id is unconditionally lowercased there (case-insensitive
  // keybinding dispatch and note-entry both need that - see that
  // lambda's own comment), which would otherwise make it impossible for
  // an active reader (track name/locator/M-x text entry, none of
  // which care about keybindings or note-entry at all) to ever receive a
  // genuine uppercase letter. getTrueCaseId() is what a reader is fed
  // instead - see UIPlane::offerInput()'s own use of it.
  Input(int id, int y, int x, bool alt, bool shift, bool ctrl, bool meta, Kind kind = Kind::UNKNOWN,
	     int true_case_id = -1)
    : id_(id), true_case_id_(true_case_id == -1 ? id : true_case_id),
      y_(y), x_(x), alt_(alt), shift_(shift), ctrl_(ctrl), meta_(meta), kind_(kind) { }

  int getId() const { return id_; }
  int getTrueCaseId() const { return true_case_id_; }
  int getY() const { return y_; }
  int getX() const { return x_; }
  bool hasAlt() const { return alt_; }
  bool hasShift() const { return shift_; }
  bool hasCtrl() const { return ctrl_; }
  bool hasMeta() const { return meta_; }
  Kind getKind() const { return kind_; }
  

 private:
  int id_, true_case_id_, y_, x_;
  bool alt_, shift_, ctrl_, meta_;
  Kind kind_;
};

}  // namespace cellui
