#include "UI.h"

#include "Chart.h"
#include "InfoLine.h"
#include "StatusLine.h"
#include "ScoreDisplay.h"
#include "InstrumentList.h"

using namespace std;

void
UI::initialize() {
  // chart and volume are missing
  score_display = make_shared<ScoreDisplay>(getPlane());
  info_line = make_shared<InfoLine>(getPlane());
  status_line = make_shared<StatusLine>(getPlane());
  instrument_list = make_shared<InstrumentList>(getPlane());
}

void
UI::layout() {
  auto [ rows, cols ] = getDim();

#if 0
  root_plane->cursor_move(5, 0);
  root_plane->hline(Cell('-'), cols);
  root_plane->cursor_move(1, left_width);
  root_plane->vline(Cell('|'), 4);
#endif
  
  chart->resize(4, cols - 40).move(1, 0);
  instrument_list->resize(4, 39).move(1, cols - 40);
  volume_meter->resize(rows - 3, 1).move(1, cols - 1);
  score_display->resize(rows - 7, cols - 1).move(5, 0);
  info_line->resize(1, cols).move(rows - 2, 0);
  status_line->resize(1, cols - 1).move(rows - 1, 0);
}
