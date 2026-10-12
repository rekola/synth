#pragma once

#ifndef PRETERUNICODEBASE

// Key ids above the Unicode range, the values notcurses uses, so the two
// can be included together while the application moves off it. A key that
// produces a character is that character's codepoint instead.
#define PRETERUNICODEBASE 1115000
#define preterunicode(w) ((w) + PRETERUNICODEBASE)

#define NCKEY_INVALID   preterunicode(0)
#define NCKEY_RESIZE    preterunicode(1)
#define NCKEY_UP        preterunicode(2)
#define NCKEY_RIGHT     preterunicode(3)
#define NCKEY_DOWN      preterunicode(4)
#define NCKEY_LEFT      preterunicode(5)
#define NCKEY_INS       preterunicode(6)
#define NCKEY_DEL       preterunicode(7)
#define NCKEY_BACKSPACE preterunicode(8)
#define NCKEY_PGDOWN    preterunicode(9)
#define NCKEY_PGUP      preterunicode(10)
#define NCKEY_HOME      preterunicode(11)
#define NCKEY_END       preterunicode(12)
#define NCKEY_F00       preterunicode(20)
#define NCKEY_F01       preterunicode(21)
#define NCKEY_F02       preterunicode(22)
#define NCKEY_F03       preterunicode(23)
#define NCKEY_F04       preterunicode(24)
#define NCKEY_F05       preterunicode(25)
#define NCKEY_F06       preterunicode(26)
#define NCKEY_F07       preterunicode(27)
#define NCKEY_F08       preterunicode(28)
#define NCKEY_F09       preterunicode(29)
#define NCKEY_F10       preterunicode(30)
#define NCKEY_F11       preterunicode(31)
#define NCKEY_F12       preterunicode(32)
#define NCKEY_F13       preterunicode(33)
#define NCKEY_F14       preterunicode(34)
#define NCKEY_F15       preterunicode(35)
#define NCKEY_F16       preterunicode(36)
#define NCKEY_F17       preterunicode(37)
#define NCKEY_F18       preterunicode(38)
#define NCKEY_F19       preterunicode(39)
#define NCKEY_F20       preterunicode(40)
#define NCKEY_F21       preterunicode(41)
#define NCKEY_F22       preterunicode(42)
#define NCKEY_F23       preterunicode(43)
#define NCKEY_F24       preterunicode(44)
#define NCKEY_F25       preterunicode(45)
#define NCKEY_F26       preterunicode(46)
#define NCKEY_F27       preterunicode(47)
#define NCKEY_F28       preterunicode(48)
#define NCKEY_F29       preterunicode(49)
#define NCKEY_F30       preterunicode(50)
#define NCKEY_F31       preterunicode(51)
#define NCKEY_F32       preterunicode(52)
#define NCKEY_F33       preterunicode(53)
#define NCKEY_F34       preterunicode(54)
#define NCKEY_F35       preterunicode(55)
#define NCKEY_F36       preterunicode(56)
#define NCKEY_F37       preterunicode(57)
#define NCKEY_F38       preterunicode(58)
#define NCKEY_F39       preterunicode(59)
#define NCKEY_F40       preterunicode(60)
#define NCKEY_F41       preterunicode(61)
#define NCKEY_F42       preterunicode(62)
#define NCKEY_F43       preterunicode(63)
#define NCKEY_F44       preterunicode(64)
#define NCKEY_F45       preterunicode(65)
#define NCKEY_F46       preterunicode(66)
#define NCKEY_F47       preterunicode(67)
#define NCKEY_F48       preterunicode(68)
#define NCKEY_F49       preterunicode(69)
#define NCKEY_F50       preterunicode(70)
#define NCKEY_F51       preterunicode(71)
#define NCKEY_F52       preterunicode(72)
#define NCKEY_F53       preterunicode(73)
#define NCKEY_F54       preterunicode(74)
#define NCKEY_F55       preterunicode(75)
#define NCKEY_F56       preterunicode(76)
#define NCKEY_F57       preterunicode(77)
#define NCKEY_F58       preterunicode(78)
#define NCKEY_F59       preterunicode(79)
#define NCKEY_F60       preterunicode(80)
#define NCKEY_ENTER        preterunicode(121)
#define NCKEY_CLS          preterunicode(122)
#define NCKEY_DLEFT        preterunicode(123)
#define NCKEY_DRIGHT       preterunicode(124)
#define NCKEY_ULEFT        preterunicode(125)
#define NCKEY_URIGHT       preterunicode(126)
#define NCKEY_CENTER       preterunicode(127)
#define NCKEY_BEGIN        preterunicode(128)
#define NCKEY_CANCEL       preterunicode(129)
#define NCKEY_CLOSE        preterunicode(130)
#define NCKEY_COMMAND      preterunicode(131)
#define NCKEY_COPY         preterunicode(132)
#define NCKEY_EXIT         preterunicode(133)
#define NCKEY_PRINT        preterunicode(134)
#define NCKEY_REFRESH      preterunicode(135)
#define NCKEY_SEPARATOR    preterunicode(136)
#define NCKEY_CAPS_LOCK    preterunicode(150)
#define NCKEY_SCROLL_LOCK  preterunicode(151)
#define NCKEY_NUM_LOCK     preterunicode(152)
#define NCKEY_PRINT_SCREEN preterunicode(153)
#define NCKEY_PAUSE        preterunicode(154)
#define NCKEY_MENU         preterunicode(155)
#define NCKEY_MEDIA_PLAY   preterunicode(158)
#define NCKEY_MEDIA_PAUSE  preterunicode(159)
#define NCKEY_MEDIA_PPAUSE preterunicode(160)
#define NCKEY_MEDIA_REV    preterunicode(161)
#define NCKEY_MEDIA_STOP   preterunicode(162)
#define NCKEY_MEDIA_FF     preterunicode(163)
#define NCKEY_MEDIA_REWIND preterunicode(164)
#define NCKEY_MEDIA_NEXT   preterunicode(165)
#define NCKEY_MEDIA_PREV   preterunicode(166)
#define NCKEY_MEDIA_RECORD preterunicode(167)
#define NCKEY_MEDIA_LVOL   preterunicode(168)
#define NCKEY_MEDIA_RVOL   preterunicode(169)
#define NCKEY_MEDIA_MUTE   preterunicode(170)
#define NCKEY_LSHIFT       preterunicode(171)
#define NCKEY_LCTRL        preterunicode(172)
#define NCKEY_LALT         preterunicode(173)
#define NCKEY_LSUPER       preterunicode(174)
#define NCKEY_LHYPER       preterunicode(175)
#define NCKEY_LMETA        preterunicode(176)
#define NCKEY_RSHIFT       preterunicode(177)
#define NCKEY_RCTRL        preterunicode(178)
#define NCKEY_RALT         preterunicode(179)
#define NCKEY_RSUPER       preterunicode(180)
#define NCKEY_RHYPER       preterunicode(181)
#define NCKEY_RMETA        preterunicode(182)
#define NCKEY_L3SHIFT      preterunicode(183)
#define NCKEY_L5SHIFT      preterunicode(184)
#define NCKEY_MOTION       preterunicode(200)
#define NCKEY_BUTTON1      preterunicode(201)
#define NCKEY_BUTTON2      preterunicode(202)
#define NCKEY_BUTTON3      preterunicode(203)
#define NCKEY_BUTTON4      preterunicode(204)
#define NCKEY_BUTTON5      preterunicode(205)
#define NCKEY_BUTTON6      preterunicode(206)
#define NCKEY_BUTTON7      preterunicode(207)
#define NCKEY_BUTTON8      preterunicode(208)
#define NCKEY_BUTTON9      preterunicode(209)
#define NCKEY_BUTTON10     preterunicode(210)
#define NCKEY_BUTTON11     preterunicode(211)
#define NCKEY_SIGNAL       preterunicode(400)
#define NCKEY_EOF          preterunicode(500)

#define NCKEY_TAB       0x09
#define NCKEY_ESC       0x1b
#define NCKEY_SPACE     0x20

#endif

// Synthetic, non-notcurses key ids: no real Ctrl-modified numeric-keypad
// Divide/Multiply key exists in notcurses's own enum, nor does notcurses
// recognize the legacy VT220/xterm "SS3 + modifier digit" encoding some
// terminals still send for them (ESC 'O' '5' 'o'/'j') as a single key
// event at all - the terminal's escape-sequence recognizer synthesizes
// inputs carrying these ids instead of leaking the 4 raw bytes through as
// if they'd been typed. Defined
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
  // real press/repeat/release for each physical key. A terminal with no such support (notcurses
  // reports NCTYPE_UNKNOWN for literally every keystroke there, its own
  // signal that the terminal never negotiated the protocol at all - no
  // capability query needed) can't distinguish a fresh press from a held
  // key's auto-repeat or its eventual release, so UNKNOWN is kept
  // distinct from PRESS rather than folded into it: code that tracks
  // "this key is currently held" (note-entry's active_keyboard_notes_,
  // the realtime-auto-play-while-held feature) must treat UNKNOWN as "no
  // hold information available" and fall back to the old one-shot-per-
  // keystroke behavior, not silently mistake it for a real, eventually-
  // released press.
  enum class Kind { UNKNOWN, PRESS, REPEAT, RELEASE };

  // true_case_id defaults to id itself - only the terminal's key reader
  // passes a distinct value, for a plain letter key: id is lowercased there
  // (case-insensitive keybinding dispatch and note-entry both need that),
  // which would otherwise make it impossible for an active text reader
  // (a name or command prompt, which cares about neither) to ever receive
  // a genuine uppercase letter. getTrueCaseId() is what a reader is fed
  // instead.
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
