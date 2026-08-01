#include "UI.h"

#include "UIMenu.h"
#include "Chart.h"
#include "HeatmapChart.h"
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
#include "AudioBlockEvent.h"
#include "VisualizationResultEvent.h"
#include "VisualizationThread.h"
#include "Controller.h"
#include "KeyChord.h"
#include "LaunchpadButtonEvent.h"
#include "LaunchpadPadEvent.h"
#include "LaunchpadProtocol.h"
#include "LaunchpadManager.h"

#include <fmt/core.h>
#include <thread>
#include <array>
#include <cmath>

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

  constexpr int kHeatmapWidth = 31; // 20 * 1.5, rounded up to the nearest odd width
  constexpr int kScopeRow = 1, kScopeHeight = 5;
  int chart_width = cols - 9 - kHeatmapWidth - 2; // -2 for the single-column dividers on either side of the heatmap
  int divider1_x = chart_width, divider2_x = divider1_x + 1 + kHeatmapWidth;
  chart_->resize(kScopeHeight, chart_width).move(kScopeRow, 0);
  heatmap_->resize(kScopeHeight, kHeatmapWidth).move(kScopeRow, divider1_x + 1);
  volume_meter_->resize(kScopeHeight, 9).move(kScopeRow, divider2_x + 1);

  // Single-column dividers between the three scopes - drawn once here
  // rather than per-frame, since these columns fall outside every scope's
  // own resized rectangle, so nothing else ever repaints over them.
  setFgColor(styles_.window_border_color);
  setBgColor(styles_.window_bg_color);
  for (int row = 0; row < kScopeHeight; row++) {
    putstr(kScopeRow + row, divider1_x, "│");
    putstr(kScopeRow + row, divider2_x, "│");
  }
  pattern_editor_->resize(rows - 8, cols).move(6, 0);
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

  if (launchpad_manager_) {
    auto & song = getController().getSong();
    auto track_ids = song.getRootTrackIds();
    launchpad_manager_->refresh(song, track_ids, getController().getPlaybackInfo(),
      track_ids.empty() ? -1 : pattern_editor_->getCursorTrackIndex());
  }

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

    bool activated = tryActivate(input.getY(), input.getX(), status_line_) ||
      tryActivate(input.getY(), input.getX(), pattern_editor_);

    for (auto & window : windows_) {
      activated = tryActivate(input.getY(), input.getX(), window) || activated;
    }

    // Fall back to the pattern editor - the default/main workspace - if the
    // click landed somewhere no widget claims (e.g. the FFT/heatmap/loudness
    // scope strip, or the dividers between them). Without this, active_element_
    // was left permanently empty (a real, confirmed bug): every subsequent
    // keyboard command routed through it (UI::offerInput()'s active_element_
    // fallback below, and Launchpad button commands via UI::executeCommand())
    // silently no-op'd - including plain Up/Down arrow - until the user
    // happened to click directly back on the pattern editor.
    if (!activated) active_element_ = pattern_editor_;
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

  // Must run right after setPlaybackInfo() above, before any other event
  // (a pad press, a keystroke) that might read the just-updated row and
  // write a note there - a no-op outside an active realtime-recording
  // session, and cheap even then (only rows the playhead actually just
  // passed get touched). This ordering is what makes the whole-row-clear
  // feature race-free: by the time any note write can possibly see the
  // new row, the clear for it (if any) has already happened, all within
  // this same synchronous call - see LaunchpadManager::onRowAdvanced()'s
  // own comment for the full reasoning.
  if (launchpad_manager_) launchpad_manager_->onRowAdvanced(getController());
  if (pattern_editor_) pattern_editor_->onRowAdvanced(getController());

  // If a newer PlaybackEvent is already queued behind this one, this one's
  // visual result is about to be immediately overwritten - skip the
  // comparatively expensive chart/meter update work for it. Doesn't change
  // what eventually gets rendered (the last event in a batch always won
  // anyway, via plain overwrite); it only avoids redoing that work once per
  // superseded event during a catch-up burst, so the app catches up faster
  // instead of falling further behind.
  bool superseded = getController().getUIEventQueue().hasEvents();
  if (!superseded) {
    // Raw, pre-mixdown per-channel levels (ambisonic bus, then always
    // AuxA/AuxB last - see Player.cpp/SongState::render()) rather than
    // the final decoded L/R output. Always fills the full fixed-size
    // domain (kMaxMeterChannels - the order-3-ambisonic+2-aux max),
    // padding with silence past the current config's real channel count -
    // matching displayFFT()'s own always-fill-the-whole-domain contract
    // (handleVisualizationResultEvent(), below - FFT results arrive via a
    // separate event/handler from VisualizationThread, its own dedicated
    // analysis thread; see VisualizationThread.h) (every index, every
    // call). Feeding a varying, sometimes-shorter range confused the
    // underlying plot's own domain/alignment
    // (bars for a smaller config visibly started mid-width instead of at
    // column 0, out of step with the legend) - a fixed domain avoids that.
    //
    // Only for events that actually carry loudness data - Player.cpp also
    // pushes info-only PlaybackEvents (song-position sync, no rendering
    // happened) with empty getChannelLoudness()/getMeterLabel(). If one of
    // those reached the meter first, its empty label would get "locked in"
    // by TerminalChart's lazy plot creation (which decides whether to
    // reserve a row for the label the first time setSample() is EVER
    // called) before any real label ever had a chance to - permanently
    // losing the reserved row for the whole session.
    auto & levels = ev.getChannelLoudness();
    if (!levels.empty()) {
      volume_meter_->setFooterLabel(ev.getMeterLabel());
      for (size_t i = 0; i < kMaxMeterChannels; i++) {
        volume_meter_->setSample(static_cast<int>(i), i < levels.size() ? levels[i] : 0.0);
      }
      volume_meter_->commit(); // chart_'s own commit() already runs inside displayFFT()
    }
  }

  ev.redraw();
}

void
UI::handleVisualizationResultEvent(VisualizationResultEvent & ev) {
  // Same superseded-skip reasoning as handlePlaybackEvent() above - both
  // share ui_event_queue, so a still-queued event behind this one means
  // this one's visual result is about to be overwritten anyway.
  bool superseded = getController().getUIEventQueue().hasEvents();
  if (!superseded) {
    if (!ev.getFFT().empty()) {
      chart_->displayFFT(ev.getFFT());
    }

    if (ev.hasDiracGrid()) {
      // plans/dirac-heatmap-scope.md SS6: displayed[cell] = the grid's own
      // directional energy plus the per-band diffuse haze, spread
      // uniformly across every cell (one shared scalar summed from all 8
      // bands, not a separate per-band-per-cell splat).
      auto & grid = ev.getDiracGrid();
      auto & diffuse_energy = ev.getDiracDiffuseEnergy();
      float diffuse_sum = 0.0f;
      for (auto e : diffuse_energy) diffuse_sum += e;
      float local_diffuse = diffuse_sum / static_cast<float>(DiracAnalyzer::kGridSize);

      std::array<float, DiracAnalyzer::kGridSize> displayed;
      float frame_max = 0.0f;
      for (size_t i = 0; i < DiracAnalyzer::kGridSize; i++) {
        displayed[i] = grid[i] + local_diffuse;
        if (displayed[i] > frame_max) frame_max = displayed[i];
      }

      // Auto-scaling brightness reference, tracked across events rather
      // than derived fresh each time: jumps up immediately on a new peak
      // (so a loud transient doesn't clip the display), decays slowly
      // otherwise (~2s time constant at this event's ~28.7Hz delivery
      // rate - plans/dirac-heatmap-scope.md SS1) so a quiet passage
      // doesn't suddenly wash the whole grid out to full brightness the
      // instant a loud part ends.
      if (frame_max > dirac_running_max_) dirac_running_max_ = frame_max;
      else dirac_running_max_ += (frame_max - dirac_running_max_) * 0.0173f;

      // log1p applied to the *ratio* to dirac_running_max_ (not to the raw
      // absolute displayed[i]/dirac_running_max_ values themselves) - taking
      // log1p of an un-normalized absolute magnitude made the compression
      // severity (and so the perceived fade time of a decaying cell) scale
      // with how loud the audio was: log1pf(displayed)/log1pf(max) only
      // approaches 0 once displayed drops below O(1) in absolute terms, so a
      // louder passage (larger running_max, in arbitrary energy units) left
      // a decayed cell sitting at a substantial fraction of full brightness
      // long after its energy had genuinely fallen away - e.g. at 1% of
      // peak it could still read ~35% bright. Normalizing to a ratio first
      // ties the curve to *relative* loudness instead, so "decayed to 1% of
      // peak" always maps to roughly the same low brightness regardless of
      // the absolute scale - kRatioCompression trades off shadow detail
      // against how quickly a decaying cell now visibly reads as "gone".
      constexpr float kRatioCompression = 16.0f;
      float log_max = log1pf(kRatioCompression);
      std::vector<float> brightness(DiracAnalyzer::kGridSize), saturation(DiracAnalyzer::kGridSize);
      for (size_t i = 0; i < DiracAnalyzer::kGridSize; i++) {
        float ratio = dirac_running_max_ > 0.0f ? displayed[i] / dirac_running_max_ : 0.0f;
        brightness[i] = log1pf(kRatioCompression * ratio) / log_max;
        if (brightness[i] > 1.0f) brightness[i] = 1.0f;
        saturation[i] = displayed[i] > 1e-12f ? grid[i] / displayed[i] : 0.0f;
      }
      heatmap_->setGrid(brightness, saturation);
      heatmap_->commit();
    }
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
  // DRAW mode (a plain coloring toy - see LaunchpadManager::
  // pressDrawPad/releaseDrawPad) touches no Song/Track/Pattern data at
  // all, unlike every other pad-event use (note entry, Send A/B/Pan) -
  // handled entirely here, before PatternEditor (which owns actual
  // pattern editing) ever sees the event.
  if (launchpad_manager_ && launchpad_manager_->gridMode(ev.getDeviceIndex()) == LaunchpadManager::GridMode::DRAW) {
    if (ev.getKind() == LaunchpadPadEvent::PRESS) {
      launchpad_manager_->pressDrawPad(ev.getDeviceIndex(), ev.getX(), ev.getY(), ev.getVelocity());
    } else if (ev.getKind() == LaunchpadPadEvent::AFTERTOUCH) {
      launchpad_manager_->updateDrawIntensity(ev.getDeviceIndex(), ev.getX(), ev.getY(), ev.getVelocity());
    } else if (ev.getKind() == LaunchpadPadEvent::RELEASE) {
      launchpad_manager_->releaseDrawPad(ev.getDeviceIndex(), ev.getX(), ev.getY());
    }
    return;
  }
  if (!launchpad_manager_) return;
  launchpad_manager_->handlePadEvent(ev, getController(),
    pattern_editor_->getCursorTrackIndex(), pattern_editor_->getEditStepSize());
}

void
UI::handleLaunchpadButtonEvent(LaunchpadButtonEvent & ev) {
  if (!launchpad_manager_) return;

  auto device_id = ev.getDeviceIndex();

  // CC97 (DRAW mode toggle) needs both press and release - a long hold,
  // released while DRAW mode is already active, clears the canvas instead
  // of toggling the mode (see LaunchpadManager::handleDrawToggleButton) -
  // so it's routed here before the press-only filter below, which every
  // other raw-CC button (and every other release) still goes through
  // unchanged.
  if (ev.getCCNumber() == 97) {
    launchpad_manager_->handleDrawToggleButton(device_id, ev.getKind() == LaunchpadButtonEvent::PRESS);
    return;
  }

  if (ev.getKind() != LaunchpadButtonEvent::PRESS) return;

  // Send A/B: a direct hardware-state toggle (this device's own transient
  // grid-display mode), never a command - intercepted here, by raw CC
  // number, before any command-name resolution happens at all. See
  // LaunchpadManager::handleRawButton's own comment.
  if (launchpad_manager_->handleRawButton(ev.getCCNumber(), device_id)) return;

  auto name = LaunchpadProtocol::commandForButton(ev.getCCNumber());
  if (!name) return;

  // Emacs prefix-argument style: resolve which track_id this specific
  // physical device currently targets and stash it as a one-shot
  // transient on Controller before dispatching - "toggle-mute" (and any
  // future command that cares) reads-and-clears it, falling back to the
  // shared cursor's own track otherwise (see PatternEditor's constructor,
  // Controller::consumePendingCommandTrack). Harmless to set
  // unconditionally, even for commands that never consume it (octave-up,
  // next-track, ...) - it's a one-shot value, overwritten or cleared by
  // the very next dispatch either way, so it can never leak into a later,
  // unrelated command.
  auto track_ids = getController().getSong().getRootTrackIds();
  getController().setPendingCommandTrack(launchpad_manager_->resolveTrackId(device_id, track_ids, pattern_editor_->getCursorTrackIndex()));

  // Pure per-device commands (octave/track-follow - no Song/Track access,
  // no keyboard/M-x equivalent) go through LaunchpadManager's own entry
  // point first; everything else (Song/Track-mutating commands like
  // "toggle-mute", or anything else registered anywhere) falls through to
  // the exact same executeCommand() a keybinding or M-x invocation uses.
  // Deliberately bypassing active_element_/Controller::sendCommand's focus
  // routing either way, to match how pad input already reaches
  // PatternEditor unconditionally (see handleLaunchpadPadEvent above) -
  // these would otherwise silently no-op whenever some other window
  // happens to have focus.
  bool handled = launchpad_manager_->handleCommand(*name, device_id, pattern_editor_->getCursorTrackIndex(), static_cast<int>(track_ids.size()));
  if (!handled) handled = executeCommand(*name);

  getController().setPendingCommandTrack(-1);

  // Keep the shared on-screen cursor following whichever track this
  // device is now assigned to, so a Launchpad button's effect (whether
  // navigation, or a mute/solo/send toggle on some other track) stays
  // visible - PatternEditor doesn't need to know why its cursor moved.
  if (handled) pattern_editor_->setCursorTrack(launchpad_manager_->assignedTrackIndex(device_id, pattern_editor_->getCursorTrackIndex()));
}

void audio_thread_func(Controller * controller, AudioAPI * audio) {
  Player player(controller->getChannelConfiguration(), controller);
  player.play(*audio);
}

void visualization_thread_func(Controller * controller, int sample_rate, int frame_count) {
  VisualizationThread visualization_thread(controller);
  visualization_thread.configure(sample_rate, frame_count);
  visualization_thread.run();
}

void
UI::start(AudioAPI & audio, LaunchpadIO & launchpad_io, LaunchpadManager & launchpad_manager) {
  launchpad_manager.setLaunchpadIO(&launchpad_io);
  launchpad_manager_ = &launchpad_manager;

  std::thread audio_thread(audio_thread_func, &(getController()), &audio);
  std::thread visualization_thread(visualization_thread_func, &(getController()), audio.getFrequency(), audio.getFrameCount());

  startUI(audio, launchpad_io);

  getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::TERMINATE));
  getController().getVisualizationQueue().push(make_unique<AudioBlockEvent>(SampleData(), SampleData()));

  audio_thread.join();
  visualization_thread.join();
}

void
StatusLogger::log(std::string s) {
  ui_->setStatus(std::move(s));
}
