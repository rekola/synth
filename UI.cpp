#include "UI.h"

#include "UIMenu.h"
#include "Chart.h"
#include "InfoLine.h"
#include "StatusLine.h"
#include "PatternEditor.h"
#include "InstrumentList.h"

#include <fmt/core.h>

using namespace std;
using namespace fmt;

void
UI::initialize() {
  // chart and volume are missing
  pattern_editor = make_shared<PatternEditor>(getPlane());
  info_line = make_shared<InfoLine>(getPlane());
  status_line = make_shared<StatusLine>(getPlane());
  instrument_list = make_shared<InstrumentList>(getPlane());

  active_element = pattern_editor;
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
  pattern_editor->resize(rows - 7, cols - 1).move(5, 0);
  info_line->resize(1, cols).move(rows - 2, 0);
  status_line->resize(1, cols - 1).move(rows - 1, 0);
}

bool
UI::renderComponents(bool refresh) {
  bool render = false;
  render |= pattern_editor->render(styles, refresh);
  render |= instrument_list->render(styles, refresh);
  render |= info_line->render(styles, refresh);
  return render;
}

bool
UI::tryActivate(int y, int x, std::shared_ptr<UIElement> element) {
  auto [pos_y, pos_x] = element->getPosition();
  auto [rows, cols] = element->getDim();

  if (y >= pos_y && y < pos_y + rows && x >= pos_x && x < pos_x + cols) {
    setStatus("active element changed");
    active_element = element;
    return true;
  } else {
    return false;
  }
}

bool
UI::offerInput(const UIInput & input) {
  // if (ni.ctrl && ni.id == 'L') notcurses_refresh(*nc, NULL, NULL);
  // if (ni.ctrl && (ni.id == 'q' || ni.id == 'Q')) close_ui = true;
  bool handled = false;
  
  if (input.getId() == NCKEY_RESIZE) {
    layout();
    refresh();
  } else if ((input.getId() == 'n' || input.getId() == 'N') && input.hasCtrl()) {
    setStatus("New song");
    getController().createNewSong();
    
    handled = true;
  } else if (input.getId() == ' ') {
    if (getController().getSongState().togglePlayback()) {
      setStatus("Playing");
    } else {
      setStatus("Stopped");
    }

    handled = true;
  } else if (input.hasCtrl() && input.getId() == 'R') {
    setStatus("Recording");
    is_recording = true;
  } else if (input.getId() == NCKEY_BUTTON1) {
    setStatus(format("mouse: {} {}", input.getY(), input.getX()));

    active_element.reset();
    
    tryActivate(input.getY(), input.getX(), status_line) ||
      tryActivate(input.getY(), input.getX(), pattern_editor) ||
      tryActivate(input.getY(), input.getX(), instrument_list) ||
      false;
  }

  if (!handled) {
    handled |= menu->offerInput(input);
    if (handled) setStatus("menu: " + menu->getSelected());
  }
  if (!handled) handled |= status_line->offerInput(input);

  if (!handled) {
    if (auto el = active_element.lock()) { 
      handled |= el->offerInput(input);
    }
  }
    
  return handled;
}

void
UI::setStatus(std::string s) {
  status_line->setMessage(s);
  render();
}
