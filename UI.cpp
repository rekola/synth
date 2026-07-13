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
#include "Controller.h"
#include "KeyChord.h"
#include "LaunchpadButtonEvent.h"
#include "LaunchpadProtocol.h"

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

  commands_.define("quit", [this]() { close_ui_ = true; });
  commands_.define("new-song", [this]() {
    setStatus("New song");
    getController().createNewSong();
  });
  commands_.define("toggle-playing", [this]() {
    bool playing = getController().togglePlaying();
    setStatus(playing ? "Playing" : "Stopped");
  });

  keymap_.bind(KeyChord::pack('q', true, false, false, false), "quit");
  keymap_.bind(KeyChord::pack('n', true, false, false, false), "new-song");
  keymap_.bind(KeyChord::pack(' ', false, false, false, false), "toggle-playing");

  assertCommandBindingsValid();

  // Lets StatusLine's M-x path (Controller::sendCommand, which only reaches
  // Controller/Song-level state) also invoke commands owned by UI or by
  // whichever widget is currently active, without Controller depending on
  // any UI type - see Controller.h's command_fallback_.
  getController().setCommandFallback([this](std::string_view name) { return executeCommand(name); });
}

bool
UI::executeCommand(std::string_view name) {
  if (auto el = active_element_.lock()) {
    if (el->executeCommand(name)) return true;
  }
  return UIElement::executeCommand(name);
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

  if (!status_line_->isReaderActive() && dispatchCommand(input)) return true;

  if (input.getId() == NCKEY_RESIZE) {
    // notcurses_refresh() is what makes notcurses acknowledge the terminal's
    // new dimensions (they're otherwise stale until this is called); it
    // must run before anything queries plane sizes or lays out against them.
    refresh();
    getPlane().refresh();
    layout();
    renderComponents(true);
  } else if (input.hasCtrl() && input.getId() == 'l') {
    // Deliberately not a dispatched command: this needs to fall through to
    // status_line_ below so its meta_pressed (M-x) state machine still gets
    // reset by an unrelated keypress, same as before this refactor.
    refresh();
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
  getController().setPlaybackInfo(ev.getInfo()); // cheap; always keep song position current

  // If a newer PlaybackEvent is already queued behind this one, this one's
  // visual result is about to be immediately overwritten - skip the
  // comparatively expensive chart/meter update work for it. Doesn't change
  // what eventually gets rendered (the last event in a batch always won
  // anyway, via plain overwrite); it only avoids redoing that work once per
  // superseded event during a catch-up burst, so the app catches up faster
  // instead of falling further behind.
  bool superseded = getController().getUIEventQueue().hasEvents();
  if (!superseded) {
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
    volume_meter_->commit(); // chart_'s own commit() already runs inside displayFFT()
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

void
UI::handleLaunchpadPadEvent(LaunchpadPadEvent & ev) {
  pattern_editor_->handleLaunchpadPadEvent(ev);
}

void
UI::handleLaunchpadButtonEvent(LaunchpadButtonEvent & ev) {
  if (ev.getKind() != LaunchpadButtonEvent::PRESS) return;

  auto name = LaunchpadProtocol::commandForButton(ev.getCCNumber());
  if (!name) return;

  // Deliberately bypassing active_element_/Controller::sendCommand's focus
  // routing here, to match how pad input already reaches PatternEditor
  // unconditionally (see handleLaunchpadPadEvent above) - most of the
  // commands a Launchpad button can reach (set-mark, toggle-mute, etc.)
  // are defined on PatternEditor's registry, and would otherwise silently
  // no-op whenever some other window happens to have focus.
  if (pattern_editor_->executeCommand(*name)) return;
  executeCommand(*name);
}

void audio_thread_func(Controller * controller, AudioAPI * audio) {
  Player player(controller->getChannelConfiguration(), controller);
  player.play(*audio);
}

void
UI::start(AudioAPI & audio, LaunchpadIO & launchpad_io) {
  pattern_editor_->setLaunchpadIO(&launchpad_io);

  std::thread audio_thread(audio_thread_func, &(getController()), &audio);

  startUI(audio, launchpad_io);

  getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::TERMINATE));

  audio_thread.join();
}

void
StatusLogger::log(std::string s) {
  ui_->setStatus(std::move(s));
}
