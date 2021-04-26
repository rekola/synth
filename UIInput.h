#ifndef _UIINPUT_H_
#define _UIINPUT_H_

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
  
class UIInput {
 public:
  UIInput(size_t _seqnum, int _id, int _y, int _x, bool _alt, bool _shift, bool _ctrl)
    : seqnum(_seqnum), id(_id), y(_y), x(_x), alt(_alt), shift(_shift), ctrl(_ctrl) { }

  size_t getSeqnum() const { return seqnum; }
  int getId() const { return id; }
  int getY() const { return y; }
  int getX() const { return x; }
  bool hasAlt() const { return alt; }
  bool hasShift() const { return shift; }
  bool hasCtrl() const { return ctrl; }

  int toMidiNote() const {
    switch (id) {
    case NCKEY_DEL:
    case NCKEY_BACKSPACE:
      return 0;

    case 'a': return 1; // OFF
      
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
    return -1;
  }

 private:
  size_t seqnum;
  int id;
  int y, x;
  bool alt, shift, ctrl;
};

#endif

