#ifndef _INPUTEVENT_H_
#define _INPUTEVENT_H_

#include "Tuning.h"

#ifndef suppuabisize

#define suppuabize(w) ((w) + 0x100000)                                                   

#define NCKEY_ESC      0x1b
#define NCKEY_SPACE    0x20

// Special composed key definitions. These values are added to 0x100000.                 
#define NCKEY_INVALID suppuabize(0)                                                      
#define NCKEY_RESIZE  suppuabize(1) // generated internally in response to SIGWINCH      
#define NCKEY_UP      suppuabize(2)                                                      
#define NCKEY_RIGHT   suppuabize(3)                                                      
#define NCKEY_DOWN    suppuabize(4)                                                      
#define NCKEY_LEFT    suppuabize(5)

#define NCKEY_DEL   suppuabize(7)
#define NCKEY_BACKSPACE   suppuabize(8)
#define NCKEY_PGDOWN  suppuabize(9)
#define NCKEY_PGUP    suppuabize(10)

#define NCKEY_ENTER   suppuabize(121)

#define NCKEY_BUTTON1  suppuabize(201)
#define NCKEY_BUTTON2  suppuabize(202)
#define NCKEY_BUTTON3  suppuabize(203)
#define NCKEY_BUTTON4  suppuabize(204) // scrollwheel up
#define NCKEY_BUTTON5  suppuabize(205) // scrollwheel down

#endif
  
class InputEvent : public Event {
 public:
  InputEvent(size_t _seqnum, int _id, int _y, int _x, bool _alt, bool _shift, bool _ctrl)
    : seqnum(_seqnum), id(_id), y(_y), x(_x), alt(_alt), shift(_shift), ctrl(_ctrl) { }

  void dispatch(EventHandler & evh) override { evh.handleInputEvent(*this); }

  size_t getSeqnum() const { return seqnum; }
  int getId() const { return id; }
  int getY() const { return y; }
  int getX() const { return x; }
  bool hasAlt() const { return alt; }
  bool hasShift() const { return shift; }
  bool hasCtrl() const { return ctrl; }

  int toMidiNote(Tuning tuning) const {
    if (id == 'a') {
      return 0; // OFF    
    } else if (tuning == Tuning::TET12) {
      switch (id) {
      case 'z': return 48;
      case 's': return 49;
      case 'x': return 50;
	
      case 'q': return 60;
      case '2': return 61;
      case 'w': return 62;
      case '3': return 63;
      case 'e': return 64;
	
      case 'r': return 65;
      case '5': return 66;
      case 't': return 67;
      case '6': return 68;
      case 'y': return 69;
      case '7': return 70;
      case 'u': return 71;
	
      case 'i': return 72;
      case '9': return 73;
      case 'o': return 74;
      case '0': return 75;
      case 'p': return 76;	
      }
    } else if (tuning == Tuning::TET31) {
      switch (id) {
      case 'z': return 155; // C-4
      case '1': return 156; // Dbb4
      case 'w': return 157; // C#4
      case 's': return 158; // Db4
      case '2': return 159; // Cx4
      case 'x': return 160; // D-4
      case '3': return 161; // Ebb4
      case 'e': return 162; // D#4
      case 'd': return 163; // Eb4
      case '4': return 164; // Dx4
      case 'c': return 165; // E-4
      case 'r': return 166; // F♭4
      case 'f': return 167; // E#4
      case 'v': return 168; // F-4
      case '5': return 169; // Gbb4
      case 't': return 170; // F#4
      case 'g': return 171; // Gb4
      case '6': return 172; // Fx4
      case 'b': return 173; // G-4
      case '7': return 174; // Abb4
      case 'y': return 175; // G#4
      case 'h': return 176; // Ab4
      case '8': return 177; // Gx4
      case 'n': return 178; // A-4
      case '9': return 179; // Bbb4
      case 'u': return 180; // A#4
      case 'j': return 181; // Bb4
      case '0': return 182; // Ax4
      case 'm': return 183; // B-4
      case 'i': return 184; // C♭5
      case 'k': return 185; // B♯4
      case ',': return 186; // C-5
      case '+': return 187; // Dbb5
      case 'o': return 188; // C#5
      case 'l': return 189; // Db5
	// case '?': return 190; // Cx5
      case '.': return 191; // D-5
	// case '?': return 192; // Ebb5
      case 'p': return 193; // D#5
	// case '?': return 194; // Eb5
	// case '?': return 195; // Dx5
      case '-': return 196; // E-5
      }
    }
    return -1;
  }

 private:
  size_t seqnum;
  int id;
  int y, x;
  bool alt, shift, ctrl;
};

#endif

