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
  pattern_editor_ = make_shared<PatternEditor>(getPlane());
  info_line_ = make_shared<InfoLine>(getPlane());
  status_line_ = make_shared<StatusLine>(getPlane());

#if 0
  windows_.push_back(make_shared<HierarchyView>(getPlane()));
#endif
  
  active_element_ = pattern_editor_;
}

void
UI::layout() { 
  auto [ rows, cols ] = getDim();
  setStatus("Layout (rows = " + to_string(rows) + ", cols = " + to_string(cols) + ")");

  chart_->resize(4, cols).move(1, 0);
  volume_meter_->resize(rows - 3, 1).move(1, cols - 1);
  pattern_editor_->resize(rows - 7, cols).move(5, 0);
  info_line_->resize(1, cols).move(rows - 2, 0);
  status_line_->resize(1, cols - 1).move(rows - 1, 0);

  for (auto & window : windows_) {
    window->resize(rows - 7, cols).move(5, 0);
  }
}

bool
UI::renderComponents(bool refresh) {
  bool render = false;
  render |= pattern_editor_->render(styles_, refresh);
#if 0
  for (auto & window : windows_) {
    render |= window->render(styles_, refresh);
  }
#endif
  render |= info_line_->render(styles_, refresh);
  return render;
}

bool
UI::tryActivate(int y, int x, std::shared_ptr<UIElement> element) {
  auto [pos_y, pos_x] = element->getPosition();
  auto [rows, cols] = element->getDim();

  if (y >= pos_y && y < pos_y + rows && x >= pos_x && x < pos_x + cols) {
    setStatus("active element changed");
    active_element_ = element;
    return true;
  } else {
    return false;
  }
}

bool
UI::offerInput(const InputEvent & input) {
  bool handled = false;
  
  if (input.getId() == NCKEY_RESIZE) {
    getPlane().refresh();
    layout();
    refresh();
  } else if (input.hasCtrl() && input.getId() == 'l') {
    refresh();
  } else if (input.hasCtrl() && input.getId() == 'q') {
    close_ui_ = true;
  } else if (input.hasCtrl() && input.getId() == 'n') {
    setStatus("New song");
    getController().createNewSong();
    
    handled = true;
  } else if (!input.hasCtrl() && input.getId() == ' ') {
    bool playing = getController().togglePlaying();
    setStatus(playing ? "Playing" : "Stopped");
    handled = true;
  } else if (input.getId() == NCKEY_BUTTON1) {
    active_element_.reset();
    
    tryActivate(input.getY(), input.getX(), status_line_) ||
      tryActivate(input.getY(), input.getX(), pattern_editor_) ||
      false;

    for (auto & window : windows_) {
      tryActivate(input.getY(), input.getX(), window);
    }
  }

  if (!handled) {
    handled |= menu_->offerInput(input);
    if (handled) setStatus("menu: " + menu_->getSelected());
  }
  if (!handled) {
    handled |= status_line_->offerInput(input);
  }

  if (!handled) {
    if (auto el = active_element_.lock()) { 
      handled |= el->offerInput(input);
    }
  }
    
  return handled;
}

void
UI::setStatus(std::string s) {
  if (status_line_) {
    status_line_->setMessage(std::move(s));
    render();
  }
}

void
UI::handlePlaybackEvent(PlaybackEvent & ev) {
  getController().setPlaybackInfo(ev.getInfo());

  if (!ev.getFFT().empty()) {
    chart_->displayFFT(ev.getFFT());
  }

  if (ev.getLoudness().size() == 2) {
#if 0
    auto left = 20*log10(ev.getLoudness()[0] / 20);
    auto right = 20*log10(ev.getLoudness()[1] / 20);
#else
    auto left = ev.getLoudness()[0];
    auto right = ev.getLoudness()[1];
#endif
    volume_meter_->setSample(0, left);
    volume_meter_->setSample(1, right);
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
  pattern_editor_->handleMidiEvent(ev);
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
  ui_->setStatus(std::move(s));
}
