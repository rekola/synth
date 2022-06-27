#include "UI.h"

#include "UIMenu.h"
#include "Chart.h"
#include "InfoLine.h"
#include "StatusLine.h"
#include "PatternEditor.h"
#include "HierarchyView.h"
#include "AudioAPI.h"
#include "Player.h"

#include "PlaybackEvent.h"
#include "LogEvent.h"
#include "RecordEvent.h"
#include "PlaybackControlEvent.h"

#include <fmt/core.h>
#include <thread>

using namespace std;
using namespace fmt;

void
UI::initialize() {
  // chart and volume are missing
  pattern_editor = make_shared<PatternEditor>(getPlane());
  info_line = make_shared<InfoLine>(getPlane());
  status_line = make_shared<StatusLine>(getPlane());
  hierarchy_view = make_shared<HierarchyView>(getPlane());

  active_element = pattern_editor;
}

void
UI::layout() {
  auto [ rows, cols ] = getDim();
  
  chart->resize(4, cols).move(1, 0);
  volume_meter->resize(rows - 3, 1).move(1, cols - 1);
  hierarchy_view->resize(rows - 7, 39).move(5, cols - 40);
  pattern_editor->resize(rows - 7, cols - 40).move(5, 0);
  info_line->resize(1, cols).move(rows - 2, 0);
  status_line->resize(1, cols - 1).move(rows - 1, 0);
}

bool
UI::renderComponents(bool refresh) {
  bool render = false;
  render |= pattern_editor->render(styles, refresh);
  render |= hierarchy_view->render(styles, refresh);
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
UI::offerInput(const InputEvent & input) {
  bool handled = false;
  
  if (input.getId() == NCKEY_RESIZE) {
    layout();
    refresh();
  } else if (input.hasCtrl()) {
    if (input.getId() == 'l' || input.getId() == 'L') refresh();
    else if (input.getId() == 'q' || input.getId() == 'Q') close_ui = true;
  } else if ((input.getId() == 'n' || input.getId() == 'N') && input.hasCtrl()) {
    setStatus("New song");
    getController().createNewSong();
    
    handled = true;
  } else if (input.getId() == ' ') {
    bool playing = getController().togglePlaying();
    setStatus(playing ? "Playing" : "Stopped");
    handled = true;
  } else if (input.getId() == NCKEY_BUTTON1) {
    setStatus(format("mouse: {} {}", input.getY(), input.getX()));

    active_element.reset();
    
    tryActivate(input.getY(), input.getX(), status_line) ||
      tryActivate(input.getY(), input.getX(), pattern_editor) ||
      tryActivate(input.getY(), input.getX(), hierarchy_view) ||
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
  if (status_line) {
    status_line->setMessage(s);
    render();
  }
}

void
UI::handlePlaybackEvent(PlaybackEvent & ev) {
  getController().setPlaybackInfo(ev.getInfo());

  auto & data = ev.getData();

  if (!data.empty()) {
    waiting_data.append(data);

    if (waiting_data.size() >= 4096) {
      waiting_data.shortenToPowerofTwo();
      chart->displayFFT(waiting_data);
      waiting_data.clear();
    }

    auto [left, right] = ev.getLoudness();
    volume_meter->setSample(0, left);
    volume_meter->setSample(1, right);
  }
    
  ev.redraw();
}

void
UI::handleRecordEvent(RecordEvent & ev) {
  if (getController().isRecording()) {
    setStatus(format("recorded {} frames", ev.getData().size()));
    getController().addToSample(ev.getData());
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto & pattern = song.getPattern(info.getPatternIndex());
    pattern.setNote(info.getRowIndex(), getController().getRecordingTrackId(), 0, Note(1));    
  }
}

void
UI::handleLogEvent(LogEvent & ev) {
  setStatus(ev.getText());
}

void
UI::handleMidiEvent(MidiEvent & ev) {
  pattern_editor->handleMidiEvent(ev);
}

void audio_thread_func(Controller * controller, AudioAPI * audio) {
  Player player(controller->getChannelConfiguration(), controller);
  player.play(*audio);
}

void
UI::start(AudioAPI & audio) {
  std::thread audio_thread(audio_thread_func, &(getController()), &audio);

  startUI(audio);

  getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::TERMINATE));
  
  audio_thread.join();
}

void
StatusLogger::log(std::string s) {
  ui->setStatus(s);
}
