#ifndef _INPUTEVENT_H_
#define _INPUTEVENT_H_

#include "Event.h"
#include "Tuning.h"

#ifndef PRETERUNICODEBASE

#define PRETERUNICODEBASE 1115000                                                                                               
#define preterunicode(w) ((w) + PRETERUNICODEBASE)  

#define NCKEY_ESC      0x1b
#define NCKEY_SPACE    0x20
#define NCKEY_TAB      0x09

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
  InputEvent(int _id, int _y, int _x, bool _alt, bool _shift, bool _ctrl)
    : id(_id), y(_y), x(_x), alt(_alt), shift(_shift), ctrl(_ctrl) { }

  void dispatch(EventHandler & evh) override { evh.handleInputEvent(*this); }

  int getId() const { return id; }
  int getY() const { return y; }
  int getX() const { return x; }
  bool hasAlt() const { return alt; }
  bool hasShift() const { return shift; }
  bool hasCtrl() const { return ctrl; }

  int toMidiNote(int octave, Tuning tuning) const {
    if (tuning == Tuning::TET12) {
      int base = (octave - 4) * 12;
      switch (id) {
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
      switch (id) {
      case 'z': return base + 155; // C-4
      case 'x': return base + 158; // Db4
      case 'c': return base + 160; // D-4
      case 'v': return base + 162; // D#4
      case 'b': return base + 165; // E-4
      case 'n': return base + 168; // F-4
      case 'm': return base + 173; // G-4
      case ',': return base + 178; // A-4
      case 'l': return base + 179; // B𝄫4
      case '.': return base + 180; // A#4
      case 'p': return base + 181; // Bb4
      case '-': return base + 183; // B-4

      case 's': return base + 157; // C#4
      case 'd': return base + 158; // Db4
	// case 'f': return base + ?;
      case 'g': return base + 163; // Eb4
      case 'j': return base + 170; // F#4
      }
    }
    return -1;
  }

 private:
  int id, y, x;
  bool alt, shift, ctrl;
};

#endif

