#ifndef _UIINPUT_H_
#define _UIINPUT_H_

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

