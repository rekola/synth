#ifndef _INPUTEVENT_H_
#define _INPUTEVENT_H_

#include "Event.h"
#include "Tuning.h"

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

#define NCKEY_BUTTON1  preterunicode(201)
#define NCKEY_BUTTON2  preterunicode(202)
#define NCKEY_BUTTON3  preterunicode(203)
#define NCKEY_BUTTON4  preterunicode(204) // scrollwheel up
#define NCKEY_BUTTON5  preterunicode(205) // scrollwheel down

#endif
  
class InputEvent : public Event {
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

  InputEvent(int id, int y, int x, bool alt, bool shift, bool ctrl, bool meta, Kind kind = Kind::UNKNOWN)
    : id_(id), y_(y), x_(x), alt_(alt), shift_(shift), ctrl_(ctrl), meta_(meta), kind_(kind) { }

  void dispatch(EventHandler & evh) override { evh.handleInputEvent(*this); }

  int getId() const { return id_; }
  int getY() const { return y_; }
  int getX() const { return x_; }
  bool hasAlt() const { return alt_; }
  bool hasShift() const { return shift_; }
  bool hasCtrl() const { return ctrl_; }
  bool hasMeta() const { return meta_; }
  Kind getKind() const { return kind_; }
  
  int toMidiNote(int octave, Tuning tuning) const {
    if (tuning == Tuning::PERCUSSION) {
      switch (id_) {
      case '1': return 27; // High Q
      case '2': return 28; // Slap
      case '3': return 29; // Stratch Push
      case '4': return 30; // Stratch Pull
      case '5': return 31; // Sticks
      case '6': return 32; // Square Click
      case '7': return 33; // Metronome Click
      case '8': return 34; // Metronome Bell
      case '9': return 35; // Acoustic Bass Drum
      case '0': return 36; // Electric Bass Drum
      case 'q': return 37; // Side Stick
      case 'w': return 38; // Acoustic Snare
      case 'e': return 39; // Hand Clap
      case 'r': return 40; // Electric Snare
      case 't': return 41; // Low Floor Tom
      case 'y': return 42; // Closed Hi-hat
      case 'u': return 43; // High Floor Tom
      case 'i': return 44; // Pedal Hi-hat
      case 'o': return 45; // Low Tom
      case 'p': return 46; // Open Hi-hat
      case 's': return 47; // Low-Mid Tom
      case 'd': return 48; // Hi-Mid Tom
      case 'f': return 49; // Crash Cymbal 1
      case 'g': return 50; // High Tom
      case 'h': return 51; // Ride Cymbal 1
      case 'j': return 52; // Chinese Cymbal
      case 'k': return 53; // Ride Bell
      case 'l': return 54; // Tambourine
      case 'z': return 55; // Splash Cymbal
      case 'x': return 56; // Cowbell
      case 'c': return 57; // Crash Cymbal 2
      case 'v': return 58; // Vibra Slap
      case 'b': return 59; // Ride Cymbal 2
      case 'n': return 60; // High Bongo
      case 'm': return 61; // Low Bongo
      case '.': return 62; // Mute High Conga
      case ',': return 63; // Open High Conga
      case '-': return 64; // Low Conga
	// 65 High Timbale
	// 66 Low Timbale
	// 67 High Agogô
	// 68 Low Agogô
	// 69 Cabasa
	// 70 Maracas
	// 71 Short Whistle
	// 72 Long Whistle
	// 73 Short Guiro
	// 74 Long Guiro
	// 75 Claves
	// 76 High Woodblock
	// 77 Low Woodblock
	// 78 Mute Cuica
	// 79 Open Cuica
	// 80 Mute Triangle
	// 81 Open Triangle
	// 82 Shaker
      }
    } else if (tuning == Tuning::TET12) {
      int base = (octave - 4) * 12;
      switch (id_) {
      case 'z': return base + 48;
      case 's': return base + 49;
      case 'x': return base + 50;
	
      case 'q': return base + 60;
      case '2': return base + 61;
      case 'w': return base + 62;
      case '3': return base + 63;
      case 'e': return base + 64;
	
      case 'r': return base + 65;
      case '5': return base + 66;
      case 't': return base + 67;
      case '6': return base + 68;
      case 'y': return base + 69;
      case '7': return base + 70;
      case 'u': return base + 71;
	
      case 'i': return base + 72;
      case '9': return base + 73;
      case 'o': return base + 74;
      case '0': return base + 75;
      case 'p': return base + 76;	
      }
    } else if (tuning == Tuning::TET31) {
      int base = (octave - 4) * 31;
      switch (id_) {
      case 'z': return base + 155; // C
      case '1': return base + 157; // C#
      case 'q': return base + 158; // Db
      case '2': return base + 159; // Cx
      case 'w': return base + 160; // D
      case 's': return base + 161; // E𝄫
      case 'x': return base + 162; // D#
      case 'd': return base + 163; // Eb
      case 'c': return base + 165; // E
      case 'v': return base + 168; // F
      case 'g': return base + 169; // G𝄫
      case 'b': return base + 173; // G
      case 'h': return base + 176; // Ab
      case 'u': return base + 177; // G𝄪
      case 'n': return base + 178; // A
      case 'j': return base + 179; // B𝄫
      case 'm': return base + 180; // A#
      case 'k': return base + 181; // Bb
      case 'i': return base + 182; // A𝄪
      case '8': return base + 183; // B
      case ',': return base + 186; // C'
      case 'l': return base + 192; // E𝄫'
      case '.': return base + 193; // D#'
      case '-': return base + 196; // E'
      }
    } else if (tuning == Tuning::TET53) {
      int base = (octave - 4) * 53;
      switch (id_) {
      case 'z': return base + 265; // C
      case '1': return base + 268; // C#
      case 'q': return base + 270; // Db
      case '2': return base + 272; // Cx
      case 'w': return base + 274; // D
      case 's': return base + 275; // E𝄫
      case 'x': return base + 277; // D#
      case 'd': return base + 279; // Eb
      case 'c': return base + 282; // E
      case 'v': return base + 287; // F
      case 'g': return base + 289; // G𝄫
      case 'b': return base + 296; // G
      case 'h': return base + 301; // Ab
      case 'u': return base + 303; // G𝄪
      case 'n': return base + 304; // A
      case 'j': return base + 306; // B𝄫
      case 'm': return base + 308; // A𝄰
      case 'k': return base + 310; // Bb
      case 'i': return base + 311; // A𝄪
      case '8': return base + 313; // B
      case ',': return base + 53 + 265; // C'
      case 'l': return base + 53 + 275; // E𝄫'
      case '.': return base + 53 + 277; // D#'
      case '-': return base + 53 + 282; // E'
      }
    }
    return -1;
  }

 private:
  int id_, y_, x_;
  bool alt_, shift_, ctrl_, meta_;
  Kind kind_;
};

#endif

