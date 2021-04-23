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
  
 private:
  size_t seqnum;
  int id;
  int y, x;
  bool alt, shift, ctrl;
};

#endif

