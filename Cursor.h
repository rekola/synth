#ifndef _CURSOR_H_
#define _CURSOR_H_

class Cursor {
 public:
  Cursor() { }

  bool isHighlighted(size_t _track, size_t _col) const { return _track == track && _col == col; }

  size_t start_track = 0;
  size_t track = 0, col = 0, subcol = 0;
};

#endif
