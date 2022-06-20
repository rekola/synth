#ifndef _CURSOR_H_
#define _CURSOR_H_

class Cursor {
 public:
  Cursor() { }

  bool isHighlighted(int _track, int _col) const { return _track == track && _col == col; }

  int start_track = 0;
  int track = 0, col = 0, subcol = 0;
};

#endif
