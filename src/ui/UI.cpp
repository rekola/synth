#include "UI.h"
#include "AboutText.h"

#include "../audio/AudioAPI.h"
#include "../audio/AudioBuffer.h"
#include "../audio/AudioDevices.h"
#include "../launchpad/LaunchpadIO.h"
#include "../launchpad/LaunchpadManager.h"
#include "../launchpad/LaunchpadChannelPressureEvent.h"
#include "../launchpad/LaunchpadPadEvent.h"
#include "../launchpad/LaunchpadButtonEvent.h"
#include "../launchpad/LaunchpadProtocol.h"
#include "../util/ShutdownSignal.h"
#include "../playback/Player.h"
#include "../playback/PlaybackControlEvent.h"
#include "../playback/AudioBlockEvent.h"
#include "../playback/VisualizationThread.h"
#include "../Controller.h"
#include "../model/Group.h"
#include "../model/InstrumentTrack.h"
#include "../model/PercussionTrack.h"
#include "../model/SampleTrack.h"
#include "../model/Song.h"
#include "../model/ArrangementOps.h"
#include "../playback/RecordEvent.h"
#include "../playback/RecordingLatencyEvent.h"
#include "../playback/ThresholdRecordingTriggeredEvent.h"

#include <algorithm>
#include <fmt/core.h>
#include <memory>
#include <thread>

using namespace std;

namespace {

void audio_thread_func(Controller * controller, AudioAPI * audio) {
  Player player(controller->getChannelConfiguration(), controller);
  player.play(*audio);
}

void visualization_thread_func(Controller * controller, int sample_rate, int frame_count) {
  VisualizationThread visualization_thread(controller);
  visualization_thread.configure(sample_rate, frame_count);
  visualization_thread.run();
}

} // namespace

int
UI::indexOfTrack(const std::vector<int> & track_ids, int track_id) {
  auto it = std::find(track_ids.begin(), track_ids.end(), track_id);
  return it == track_ids.end() ? 0 : static_cast<int>(it - track_ids.begin());
}

bool
UI::shouldClose() const {
  return close_ui_ || shutdownRequested();
}

void UI::selectCaptureDevice(const std::string & name, const std::string & label) {
  auto change = getController().setCaptureDevice(name);
  if (!change.applied)
    setStatus(change.message);
  else
    setStatus("Switching capture device to " + label + (change.message.empty() ? "" : " (" + change.message + ")"));
}

void UI::selectPlaybackDevice(const std::string & name, const std::string & label) {
  auto change = getController().setPlaybackDevice(name);
  if (!change.applied)
    setStatus(change.message);
  else
    setStatus("Switching playback device to " + label + (change.message.empty() ? "" : " (" + change.message + ")"));
}

void UI::selectMidiInput(const std::string & spec, const std::string & label) {
  if (!audio_) return;
  audio_->setMidiInput(spec, getLogger());
  auto change = getController().setMidiInput(spec);
  if (!change.message.empty()) setStatus("MIDI input " + label + " (" + change.message + ")");
}

void
UI::start(AudioAPI & audio, LaunchpadIO & launchpad_io, LaunchpadManager & launchpad_manager) {
  audio_ = &audio;

  // AlsaAudio::initialize() already logged this to stderr, before this UI
  // (and its StatusLogger) even existed - a failed/missing capture device
  // would otherwise be silently invisible for the rest of the session,
  // leaving "why does nothing ever get recorded" undiagnosable from inside
  // the running app.
  setStatus(audio.hasCaptureDevice() ?
    "Capture device: " + audio.getCaptureDeviceName() :
    "WARNING: no capture device available - recording is disabled");

  launchpad_manager.setLaunchpadIO(&launchpad_io);
  launchpad_manager_ = &launchpad_manager;
  wireLaunchpad(launchpad_manager);

  std::thread audio_thread(audio_thread_func, &(getController()), &audio);
  std::thread visualization_thread(visualization_thread_func, &(getController()), audio.getFrequency(), audio.getFrameCount());

  startUI(audio, launchpad_io);

  getController().getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::TERMINATE));
  getController().getVisualizationQueue().push(make_unique<AudioBlockEvent>(AudioBuffer(), AudioBuffer(), AudioBuffer(), AudioBuffer()));

  audio_thread.join();
  visualization_thread.join();
}

void
UI::handleLaunchpadChannelPressureEvent(LaunchpadChannelPressureEvent & ev) {
  if (!launchpad_manager_) return;
  launchpad_manager_->handleChannelPressureEvent(ev, getController());
}

void
UI::handleLaunchpadPadEvent(LaunchpadPadEvent & ev) {
  // Track-picker overlay (opened by CC49 "Stop Clip"/CC39 "Mute"/CC29
  // "Solo" - see LaunchpadManager::handleRawButton()'s own comment) -
  // Session-view-only, so this only ever intercepts the picker row itself
  // while it's open; every other row (Session view's own content) falls
  // through to the normal SESSION handling below unchanged, staying fully
  // interactive underneath the overlay.
  if (launchpad_manager_ && launchpad_manager_->isTrackPickerRow(ev.getDeviceIndex(), ev.getY())) {
    launchpad_manager_->handleTrackPickerPadEvent(ev, getController());
    return;
  }
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
  // The Tempo/Swing views show a number; their pads do nothing.
  if (launchpad_manager_ && (launchpad_manager_->gridMode(ev.getDeviceIndex()) == LaunchpadManager::GridMode::TEMPO ||
                             launchpad_manager_->gridMode(ev.getDeviceIndex()) == LaunchpadManager::GridMode::SWING)) {
    return;
  }
  // GridMode::SESSION: unlike DRAW above, this one does need Controller -
  // an "assign" press writes into the Song directly, and either sub-mode
  // (audition/assign - see handleSessionPadEvent()'s own comment) needs
  // the playback event queue.
  if (launchpad_manager_ && launchpad_manager_->gridMode(ev.getDeviceIndex()) == LaunchpadManager::GridMode::SESSION) {
    launchpad_manager_->handleSessionPadEvent(ev, getController());
    return;
  }
  if (!launchpad_manager_) return;
  // handlePadEvent() itself indexes song.getPlayableTrackIds() with this
  // - see indexOfTrack()'s own comment for why a real index, not the bare
  // id, is still what it needs.
  auto track_ids = getController().getSong().getPlayableTrackIds();
  launchpad_manager_->handlePadEvent(ev, getController(),
    indexOfTrack(track_ids, getController().getSong().getCurrentTrackId()), launchpadEditStepSize());
}

void
UI::handleLaunchpadButtonEvent(LaunchpadButtonEvent & ev) {
  if (!launchpad_manager_) return;

  auto device_id = ev.getDeviceIndex();

  // Resolves and dispatches whatever command a raw CC number names
  // (LaunchpadProtocol::commandForButton()) - factored out since CC91's
  // own deferred shift-tap below (handleShiftButton()'s own comment)
  // needs to reach this exact same path from a release event instead of
  // the ordinary press-driven call site further down.
  auto dispatch_named_command = [&](int cc_number) {
    auto name = LaunchpadProtocol::commandForButton(cc_number);
    if (!name) return;

    // Emacs prefix-argument style: resolve which track_id this specific
    // physical device currently targets and stash it as a one-shot
    // transient on Controller before dispatching - "toggle-mute" (and any
    // future command that cares) reads-and-clears it, falling back to the
    // shared cursor's own track otherwise (see PatternEditor's constructor,
    // Controller::consumePendingCommandTrack). Harmless to set
    // unconditionally, even for commands that never consume it (octave-up,
    // pad-next-track, ...) - it's a one-shot value, overwritten or cleared by
    // the very next dispatch either way, so it can never leak into a later,
    // unrelated command.
    auto track_ids = getController().getSong().getPlayableTrackIds();
    getController().setPendingCommandTrack(launchpad_manager_->resolveTrackId(device_id, track_ids, indexOfTrack(track_ids, getController().getSong().getCurrentTrackId())));

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
    bool handled = launchpad_manager_->handleCommand(*name, device_id, indexOfTrack(track_ids, getController().getSong().getCurrentTrackId()), static_cast<int>(track_ids.size()), getController());
    if (!handled) handled = executeLaunchpadCommand(*name);

    getController().setPendingCommandTrack(-1);
  };

  // CC98 (Session Record) needs press and release, not just press - its
  // own tap-vs-long-hold gesture (LaunchpadManager::handleRecordButton()):
  // a quick tap overdubs the playing clip (or stops the takes in flight),
  // a long hold is Capture MIDI. Routed here before the press-only filter
  // below, which every other raw-CC button (and every other release)
  // still goes through unchanged.
  if (ev.getCCNumber() == 98) {
    launchpad_manager_->handleRecordButton(device_id, getController(), ev.getKind() == LaunchpadButtonEvent::PRESS);
    return;
  }

  // CC91 ("move-row-up") doubles as a held shift modifier for opening a
  // Session-view clip's own step grid directly instead of triggering it
  // (LaunchpadManager::handleShiftButton(), DeviceState::
  // row_up_shift_held's own comment) - needs press and release too, same
  // reasoning as CC98 above: nothing about a shift-combo can be decided
  // from a press alone. handleShiftButton() itself decides whether
  // "move-row-up" should still fire (a plain tap, nothing combined) -
  // deferred to here, its release, rather than commandForButton()'s own
  // ordinary press-driven call site further down.
  if (ev.getCCNumber() == 91) {
    if (launchpad_manager_->handleShiftButton(device_id, ev.getKind() == LaunchpadButtonEvent::PRESS, getController())) dispatch_named_command(91);
    return;
  }

  // CC92 ("move-row-down") is the Tempo/Swing views' down arrow, whose
  // hold auto-repeats until released.
  if (ev.getCCNumber() == 92 && ev.getKind() != LaunchpadButtonEvent::PRESS) {
    launchpad_manager_->handleArrowRelease(device_id, 92);
    return;
  }

  // The mixer radio group's own ten CC numbers (Record Arm/Volume/Pan/
  // Send A/Send B/Stop Clip/Mute/Solo, Pro MK3's Mute/Solo twins -
  // LaunchpadManager::isMixerFunctionButton()) need release too, for their
  // own momentary hold-to-preview gesture
  // (LaunchpadManager::handleMixerFunctionRelease()) - same reasoning as
  // CC98 above, just a release-only rather than a press-and-release
  // handler, since the press half is still handleRawButton()'s own
  // press-only entry point below, unchanged.
  if (LaunchpadManager::isMixerFunctionButton(ev.getCCNumber())) {
    if (ev.getKind() != LaunchpadButtonEvent::PRESS) {
      launchpad_manager_->handleMixerFunctionRelease(device_id, ev.getCCNumber(), getController());
      return;
    }
  } else if (ev.getKind() != LaunchpadButtonEvent::PRESS) {
    return;
  }

  // Send A/B: a direct hardware-state toggle (this device's own transient
  // grid-display mode), never a command - intercepted here, by raw CC
  // number, before any command-name resolution happens at all. See
  // LaunchpadManager::handleRawButton's own comment.
  if (launchpad_manager_->handleRawButton(ev.getCCNumber(), device_id, getController())) return;

  dispatch_named_command(ev.getCCNumber());
}

void
UI::handleRecordEvent(RecordEvent & ev) {
  if (getController().isRecording()) {
    setStatus(fmt::format("recorded {} frames", ev.getData().size()));
    getController().addToSample(ev.getData());
    // Lazily, exactly once per take, but only for a take that was never
    // armed (Controller::isRecordingArmed()) - an armed take's own clip
    // is created by handleRecordingLatencyEvent() below instead, once its
    // own round-trip measurement arrives, so it can be placed/trimmed
    // correctly from the start rather than created here uncompensated
    // and only adjusted afterward.
    if (!getController().hasRecordingClip() && !getController().isRecordingArmed()) {
      getController().beginSampleCapture(getController().getRecordingTrackId());
    }
  }
}

void
UI::handleRecordingLatencyEvent(RecordingLatencyEvent & ev) {
  // Both guards defensive - Player.cpp only ever pushes this once per
  // take (the false -> true edge of Controller::isRecording()), and
  // hasRecordingClip() being already true would mean a second, spurious
  // measurement somehow arrived for the same take - but this is where
  // the clip actually gets created, trimmed, and placed, all as one step,
  // so it stays defensive rather than assuming either can't happen.
  if (getController().isRecording() && !getController().hasRecordingClip()) {
    getController().beginSampleCapture(getController().getRecordingTrackId(), ev.getLatencyFrames());
  }
}

void
UI::handleThresholdRecordingTriggeredEvent(ThresholdRecordingTriggeredEvent & ev) {
  // Defensive, same reasoning as handleRecordingLatencyEvent() above -
  // Player.cpp only ever pushes this once per arm cycle (its own
  // threshold_triggered_this_arm_cycle_ latch).
  if (getController().hasRecordingClip()) return;

  // This is where a threshold-triggered take actually begins, as if it
  // had been recording this whole time - startRecording() first (a fresh
  // current_sample), then (for an ordinary, non-Session-View take - see
  // below) a bar-quantized start row is derived and any gap it opens up
  // is filled with real silence, then the ring buffer's own already-
  // captured lead-in is appended, then the (possibly quantized) start
  // position is armed for beginSampleCapture() to place at.
  getController().startRecording();

  // Never for a Session View take (isSessionRecording(track_id)) - that
  // populates a clip slot directly with no arrangement position at all,
  // so there's nothing here to quantize or snapshot; beginSampleCapture()
  // already treats recording_start_row_'s own untouched -1 default as
  // "stays unplaced."
  bool is_session_recording_take = getController().isSessionRecording(ev.getTrackId());
  auto start_row = ev.getRow();
  if (!is_session_recording_take) {
    // Bar-quantized the same way ensureNoteRecordingClip() already
    // quantizes a brand-new live-recorded clip's own origin - rounded
    // back (previousBarRow()), never forward, so the take's own true
    // first frame is never placed later than it was actually captured.
    // Unlike a note's own row (just recomputed relative to the clip's
    // new, earlier origin, preserving its real timing automatically), a
    // SampleTrack clip's raw audio has no such per-frame repositioning -
    // so the gap between the quantized bar and the true (backdated)
    // onset is filled with that many frames of real silence up front
    // instead, keeping the captured content's own timing exactly where
    // it was actually performed rather than shifting the whole take
    // earlier to the bar.
    auto & song = getController().getSong();
    auto quantized_row = previousBarRow(song.getArrangementBars(), ev.getRow());
    auto gap_rows = ev.getRow() - quantized_row;
    if (gap_rows > 0) {
      auto gap_frames = gap_rows * getController().getChannelConfiguration().getSampleInterval(song.getTempo());
      if (gap_frames > 0) {
        AudioBuffer silence(1, gap_frames);
        silence.zero();
        getController().addToSample(silence);
      }
    }
    start_row = quantized_row;
  }

  getController().addToSample(ev.getPreroll());
  if (!is_session_recording_take) getController().armRecordingStart(start_row);
  getController().beginSampleCapture(ev.getTrackId());
  getController().clearThresholdArmed();
}

void
UI::initializeCommands() {
  // Every command defined here has no widget-specific dependency (a plain
  // Controller call, or setStatus() - already virtual) - shared between
  // every UI backend, so each backend's own keybindings (Emacs-style
  // chords here, whatever a future GUI uses there) just need to bind a key
  // to one of these names rather than redefine what it does. A command
  // that does need a backend-specific dialog/prompt, or reaches into a
  // concrete widget, stays defined in that backend instead (e.g.
  // TerminalUI::initializeWidgets()).
  commands_.define("about", [this]() { showInfoDialog(kAboutTitle, kAboutMarkdown); });

  // The device pickers list what is available right now (read fresh each
  // time, so a device plugged in a moment ago shows) with the one in use
  // marked. Choosing that one again changes nothing.
  auto chooseDevice = [this](const std::string & title, std::vector<Choice> choices, const std::string & current,
                             std::function<void(const Choice &)> apply) {
    int current_index = -1;
    for (size_t i = 0; i < choices.size(); i++) {
      if (choices[i].value == current || (isDefaultDevice(choices[i].value) && isDefaultDevice(current))) {
        current_index = static_cast<int>(i);
        break;
      }
    }
    auto list = choices;
    showChoiceDialog(title, std::move(choices), current_index, [list, current_index, apply](int index) {
      if (index == current_index || index < 0 || index >= static_cast<int>(list.size())) return;
      apply(list[static_cast<size_t>(index)]);
    });
  };
  // Names where the list comes from. The card list is much shorter than the
  // audio server's own, so a missing device may just mean this build can't
  // see the server.
  auto deviceDialogTitle = [](const std::string & what) {
    return what + (pipeWireAvailable() ? " (PipeWire)" : " (ALSA sound cards)");
  };
  commands_.define("select-capture-device", [this, chooseDevice, deviceDialogTitle]() {
    std::vector<Choice> choices;
    for (auto & device : listCaptureDevices()) choices.push_back({device.label, device.name});
    chooseDevice(deviceDialogTitle("Audio input"), std::move(choices), getController().getDeviceSettings().capture,
                 [this](const Choice & choice) { selectCaptureDevice(choice.value, choice.label); });
  });
  commands_.define("select-playback-device", [this, chooseDevice, deviceDialogTitle]() {
    std::vector<Choice> choices;
    for (auto & device : listPlaybackDevices()) choices.push_back({device.label, device.name});
    chooseDevice(deviceDialogTitle("Audio output"), std::move(choices), getController().getDeviceSettings().playback,
                 [this](const Choice & choice) { selectPlaybackDevice(choice.value, choice.label); });
  });
  commands_.define("select-midi-input", [this, chooseDevice]() {
    std::vector<Choice> choices;
    choices.push_back({"None", ""});
    for (auto & source : listMidiSources()) choices.push_back({source.label, source.spec});
    chooseDevice("MIDI input", std::move(choices), getController().getDeviceSettings().midi_input,
                 [this](const Choice & choice) { selectMidiInput(choice.value, choice.label); });
  });
  commands_.define("next-buffer", [this]() {
    getController().cycleBuffer(true);
  });
  commands_.define("previous-buffer", [this]() {
    getController().cycleBuffer(false);
  });
  // How the active song is shown - see UI::View. The live-sequencer
  // convention of one key flipping between the two is toggle-view; the
  // outline panel only exists in Session view, so showing it switches
  // there.
  commands_.define("arrangement-view", [this]() { setView(View::ARRANGEMENT); });
  commands_.define("session-view", [this]() { setView(View::SESSION); });
  commands_.define("cycle-monitor", [this]() { getController().cycleTrackMonitor(getController().getSong().getCurrentTrackId()); });
  commands_.define("toggle-view", [this]() { setView(view_ == View::SESSION ? View::ARRANGEMENT : View::SESSION); });
  commands_.define("toggle-outline", [this]() {
    bool show = !(outline_visible_ && view_ == View::SESSION);
    setOutlineVisible(show);
    if (show) setView(View::SESSION);
  });
  commands_.define("toggle-playing", [this]() {
    bool playing = getController().togglePlaying();
    setStatus(playing ? "Playing" : "Stopped");
  });
  // Tracks Session view took over follow the arrangement again from the
  // next bar - every one, or just the current track.
  commands_.define("back-to-arrangement", [this]() { getController().getSessionPlayer().returnAllToArrangement(); });
  commands_.define("track-back-to-arrangement", [this]() {
    getController().getSessionPlayer().returnToArrangement(getController().getSong().getCurrentTrackId());
  });
  // Global, not any one widget's own - both computer-keyboard note entry
  // and every connected Launchpad's own octave read
  // Controller::getGlobalOctave(), so these two should work regardless of
  // which widget currently has focus.
  commands_.define("octave-up", [this]() { getController().octaveUp(); });
  commands_.define("octave-down", [this]() { getController().octaveDown(); });
  commands_.define("save-song", [this]() {
    getController().sendCommand("save-song");
    setStatus("Saved " + getController().getActiveBufferName());
  });
  // Controller-level command (Controller.cpp's own definition) - targets
  // Song::getCurrentTrackId(), not any one widget's own cursor, so it
  // works regardless of which UI widget currently has focus.
  commands_.define("merge-clip-to-background", [this]() { getController().sendCommand("merge-clip-to-background"); });
  commands_.define("toggle-record-arm", [this]() { getController().sendCommand("toggle-record-arm"); });

  // Clip menu entries not built yet - say so rather than failing as an
  // unknown command.
  for (auto name : {"double-clip-length", "halve-clip-length", "toggle-clip-loop", "rename-clip"}) {
    commands_.define(name, [this, name]() { setStatus(std::string(name) + ": not implemented yet"); });
  }

  // Track commands act on Song::getCurrentTrackId(), the one current track
  // every widget keeps in sync, so they work the same from any widget and
  // from the menu. A new track lands right after the current one, under
  // whatever its real parent is (Track::insertChildAfter()) - or at the
  // very end when there's no current track. addTrack() bumps the version
  // itself.
  auto add_track = [this](std::unique_ptr<Track> track) {
    auto & song = getController().getSong();
    song.addTrack(std::move(track), song.getCurrentTrackId());
  };
  commands_.define("add-instrument-track", [add_track]() { add_track(std::make_unique<InstrumentTrack>(0)); });
  // Creating a SampleTrack and starting a take into one are separate
  // actions ("toggle-record-arm" is the latter).
  commands_.define("add-sample-track", [add_track]() { add_track(std::make_unique<SampleTrack>()); });
  commands_.define("add-percussion-track", [add_track]() { add_track(std::make_unique<PercussionTrack>()); });
  // A plain container track with no audio of its own.
  commands_.define("add-group-track", [add_track]() { add_track(std::make_unique<Group>()); });

  // Refuses to remove the last remaining root track: several pattern
  // editor code paths index the root track list with no bounds check, on
  // the assumption that at least one root track always exists (see
  // docs/known_bugs.md's zero-root-tracks entry).
  commands_.define("delete-track", [this]() {
    auto & controller = getController();
    auto & song = controller.getSong();
    if (song.getRootTrackIds().size() <= 1) return;
    auto track_id = controller.consumePendingCommandTrack(song.getCurrentTrackId());
    auto ids = song.getRootTrackIds();
    auto position = std::find(ids.begin(), ids.end(), track_id) - ids.begin();
    if (!song.removeTrack(track_id)) return;
    if (controller.getRecordingTrackId() == track_id) controller.setRecordingTrackId(0);
    // The track now at the deleted one's position becomes current.
    ids = song.getRootTrackIds();
    if (!ids.empty()) song.setCurrentTrackId(ids[static_cast<size_t>(std::min<ptrdiff_t>(position, static_cast<ptrdiff_t>(ids.size()) - 1))]);
  });
}

void
UI::setView(View view) {
  if (view == view_) return;
  view_ = view;
  viewChanged();
}

void
UI::setOutlineVisible(bool visible) {
  if (visible == outline_visible_) return;
  outline_visible_ = visible;
  viewChanged();
}

void
StatusLogger::log(std::string s) {
  ui_->setStatus(std::move(s));
}
