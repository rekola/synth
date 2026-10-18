#pragma once

#include <chrono>

namespace cellui {

// Modifier bits, in the kitty keyboard protocol's layout.
constexpr unsigned kModShift = 1;
constexpr unsigned kModAlt = 2;
constexpr unsigned kModCtrl = 4;
constexpr unsigned kModSuper = 8;
constexpr unsigned kModHyper = 16;
constexpr unsigned kModMeta = 32;
constexpr unsigned kModCapsLock = 64;
constexpr unsigned kModNumLock = 128;

// Key ids. A key that produces a character is that character's codepoint;
// the others are ids above the Unicode range, from First up. Keypad Divide and Multiply with Ctrl are keys of their own, which
// the parser reports for the legacy "SS3 + modifier digit" encoding and the
// kitty keypad codes.
namespace Key {
constexpr int First = 1115000;
constexpr int Invalid = First;
constexpr int Resize = First + 1;
constexpr int Up = First + 2;
constexpr int Right = First + 3;
constexpr int Down = First + 4;
constexpr int Left = First + 5;
constexpr int Insert = First + 6;
constexpr int Delete = First + 7;
constexpr int Backspace = First + 8;
constexpr int PageDown = First + 9;
constexpr int PageUp = First + 10;
constexpr int Home = First + 11;
constexpr int End = First + 12;
constexpr int F0 = First + 20;
constexpr int F1 = First + 21;
constexpr int F2 = First + 22;
constexpr int F3 = First + 23;
constexpr int F4 = First + 24;
constexpr int F5 = First + 25;
constexpr int F6 = First + 26;
constexpr int F7 = First + 27;
constexpr int F8 = First + 28;
constexpr int F9 = First + 29;
constexpr int F10 = First + 30;
constexpr int F11 = First + 31;
constexpr int F12 = First + 32;
constexpr int F13 = First + 33;
constexpr int F14 = First + 34;
constexpr int F15 = First + 35;
constexpr int F16 = First + 36;
constexpr int F17 = First + 37;
constexpr int F18 = First + 38;
constexpr int F19 = First + 39;
constexpr int F20 = First + 40;
constexpr int F21 = First + 41;
constexpr int F22 = First + 42;
constexpr int F23 = First + 43;
constexpr int F24 = First + 44;
constexpr int F25 = First + 45;
constexpr int F26 = First + 46;
constexpr int F27 = First + 47;
constexpr int F28 = First + 48;
constexpr int F29 = First + 49;
constexpr int F30 = First + 50;
constexpr int F31 = First + 51;
constexpr int F32 = First + 52;
constexpr int F33 = First + 53;
constexpr int F34 = First + 54;
constexpr int F35 = First + 55;
constexpr int F36 = First + 56;
constexpr int F37 = First + 57;
constexpr int F38 = First + 58;
constexpr int F39 = First + 59;
constexpr int F40 = First + 60;
constexpr int F41 = First + 61;
constexpr int F42 = First + 62;
constexpr int F43 = First + 63;
constexpr int F44 = First + 64;
constexpr int F45 = First + 65;
constexpr int F46 = First + 66;
constexpr int F47 = First + 67;
constexpr int F48 = First + 68;
constexpr int F49 = First + 69;
constexpr int F50 = First + 70;
constexpr int F51 = First + 71;
constexpr int F52 = First + 72;
constexpr int F53 = First + 73;
constexpr int F54 = First + 74;
constexpr int F55 = First + 75;
constexpr int F56 = First + 76;
constexpr int F57 = First + 77;
constexpr int F58 = First + 78;
constexpr int F59 = First + 79;
constexpr int F60 = First + 80;
constexpr int Enter = First + 121;
constexpr int Clear = First + 122;
constexpr int DownLeft = First + 123;
constexpr int DownRight = First + 124;
constexpr int UpLeft = First + 125;
constexpr int UpRight = First + 126;
constexpr int Center = First + 127;
constexpr int Begin = First + 128;
constexpr int Cancel = First + 129;
constexpr int Close = First + 130;
constexpr int Command = First + 131;
constexpr int Copy = First + 132;
constexpr int Exit = First + 133;
constexpr int Print = First + 134;
constexpr int Refresh = First + 135;
constexpr int Separator = First + 136;
constexpr int CapsLock = First + 150;
constexpr int ScrollLock = First + 151;
constexpr int NumLock = First + 152;
constexpr int PrintScreen = First + 153;
constexpr int Pause = First + 154;
constexpr int Menu = First + 155;
constexpr int MediaPlay = First + 158;
constexpr int MediaPause = First + 159;
constexpr int MediaPlayPause = First + 160;
constexpr int MediaReverse = First + 161;
constexpr int MediaStop = First + 162;
constexpr int MediaFastForward = First + 163;
constexpr int MediaRewind = First + 164;
constexpr int MediaNext = First + 165;
constexpr int MediaPrev = First + 166;
constexpr int MediaRecord = First + 167;
constexpr int MediaVolumeDown = First + 168;
constexpr int MediaVolumeUp = First + 169;
constexpr int MediaMute = First + 170;
constexpr int LeftShift = First + 171;
constexpr int LeftCtrl = First + 172;
constexpr int LeftAlt = First + 173;
constexpr int LeftSuper = First + 174;
constexpr int LeftHyper = First + 175;
constexpr int LeftMeta = First + 176;
constexpr int RightShift = First + 177;
constexpr int RightCtrl = First + 178;
constexpr int RightAlt = First + 179;
constexpr int RightSuper = First + 180;
constexpr int RightHyper = First + 181;
constexpr int RightMeta = First + 182;
constexpr int Level3Shift = First + 183;
constexpr int Level5Shift = First + 184;
constexpr int Motion = First + 200;
constexpr int Button1 = First + 201;
constexpr int Button2 = First + 202;
constexpr int Button3 = First + 203;
constexpr int Button4 = First + 204;
constexpr int Button5 = First + 205;
constexpr int Button6 = First + 206;
constexpr int Button7 = First + 207;
constexpr int Button8 = First + 208;
constexpr int Button9 = First + 209;
constexpr int Button10 = First + 210;
constexpr int Button11 = First + 211;
constexpr int Signal = First + 400;
constexpr int Eof = First + 500;
constexpr int Tab = 0x09;
constexpr int Escape = 0x1b;
constexpr int Space = 0x20;
constexpr int KpDivide = First + 900;
constexpr int KpMultiply = First + 901;
}  // namespace Key


class Input {
 public:
  // Kitty-protocol terminals (kitty, foot, wezterm, ghostty, ...) report
  // real press/repeat/release for each physical key. A terminal with no such support (the
  // parser reports UNKNOWN for every keystroke there) can't distinguish a fresh press from a held
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
  //
  // `timestamp` is when the key was read: the keys of a chord arrive within
  // a few milliseconds of each other, which is how a terminal that reports
  // no key release still shows that they were typed together.
  Input(int id, int y, int x, bool alt, bool shift, bool ctrl, bool meta, Kind kind = Kind::UNKNOWN,
	     int true_case_id = -1, std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now())
    : id_(id), true_case_id_(true_case_id == -1 ? id : true_case_id),
      y_(y), x_(x), alt_(alt), shift_(shift), ctrl_(ctrl), meta_(meta), kind_(kind), timestamp_(timestamp) { }

  int getId() const { return id_; }
  int getTrueCaseId() const { return true_case_id_; }
  int getY() const { return y_; }
  int getX() const { return x_; }
  bool hasAlt() const { return alt_; }
  bool hasShift() const { return shift_; }
  bool hasCtrl() const { return ctrl_; }
  bool hasMeta() const { return meta_; }
  Kind getKind() const { return kind_; }
  std::chrono::steady_clock::time_point getTimestamp() const { return timestamp_; }
  

 private:
  int id_, true_case_id_, y_, x_;
  bool alt_, shift_, ctrl_, meta_;
  Kind kind_;
  std::chrono::steady_clock::time_point timestamp_;
};

// The Input for a key press: an upper-case ASCII letter becomes lower case
// with Shift (without Ctrl; the original kept as the true-case id), as key
// bindings and note entry expect. Meta and Super both count as Meta.
inline Input keyInput(int key, unsigned mods, Input::Kind kind = Input::Kind::UNKNOWN) {
  bool alt = mods & kModAlt;
  bool shift = mods & kModShift;
  bool ctrl = mods & kModCtrl;
  bool meta = mods & (kModMeta | kModSuper);
  int id = key;
  if (id >= 'A' && id <= 'Z') {
    id += 'a' - 'A';
    if (!ctrl) shift = true;
  }
  return Input(id, -1, -1, alt, shift, ctrl, meta, kind, key);
}

}  // namespace cellui
