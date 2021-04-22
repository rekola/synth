#include "UI.h"

using namespace std;

void
UI::layout() {
  auto [ rows, cols ] = getDim();

#if 0
  root_plane->cursor_move(5, 0);
  root_plane->hline(Cell('-'), cols);
  root_plane->cursor_move(1, left_width);
  root_plane->vline(Cell('|'), 4);
#endif
  
  chart->resize(4, cols).move(1, 0);
  volume_meter->resize(rows - 7, 1).move(5, cols - 1);
  score_display->resize(rows - 7, cols - 1).move(5, 0);
  info_line->resize(1, cols).move(rows - 2, 0);
  status_line->resize(1, cols - 1).move(rows - 1, 0);
}
