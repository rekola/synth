#include "HeadlessUI.h"

#include "../UIPlane.h"
#include "../../launchpad/LaunchpadIO.h"
#include "../../launchpad/LaunchpadManager.h"
#include "../../audio/AudioAPI.h"
#include "../../playback/LogEvent.h"
#include "../../playback/MidiEvent.h"
#include "../../playback/PlaybackControlEvent.h"
#include "../../instruments/Tuning.h"
#include "../../playback/PlaybackEvent.h"
#include "../../playback/SessionPlayer.h"
#include "../../Controller.h"
#include "../../model/Song.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <poll.h>

using namespace std;

namespace {

// Draws nothing and takes no input; exists only because UIElement reaches
// the Controller through its plane.
class HeadlessPlane : public UIPlane {
 public:
  using UIPlane::UIPlane;

  void setFgColor(int, int, int) override { }
  void setBgColor(int, int, int) override { }
  void setUnderline(bool) override { }
  void setBold(bool) override { }
  void setItalic(bool) override { }
  void erase() override { }
  void putstr(int, int, const std::string &) override { }
  std::unique_ptr<UIPlane> createChild() override { return std::make_unique<HeadlessPlane>(getController(), getStyles()); }
  void drawBorder() override { }
  bool offerInput(const InputEvent &) override { return false; }
  void showReader(const std::string &, int, int, int, int, const std::string &, std::optional<Color>) override { }
  std::string closeReader() override { return ""; }
  bool readerActive() const override { return false; }
  std::string getReaderContents() const override { return ""; }
  void setReaderContents(const std::string &) override { }
  void showReaderIndicator(int, const std::string &) override { }
  void hideReaderIndicator() override { }
  void showPicker(int, int, int, int, int) override { }
  void addItem(const std::string &, const std::string &) override { }
  void selectPickerItem(int) override { }
  bool pickerActive() const override { return false; }
  std::string getPickerSelection() const override { return ""; }
  void closePicker() override { }
  void refresh() override { }
};

// Roughly a frame at 50 Hz - how often the session player and Launchpad
// LEDs get serviced when no event wakes the loop sooner.
constexpr int kTickIntervalMs = 20;

} // namespace

void
HeadlessUI::initialize(std::shared_ptr<Controller> & controller) {
  setPlane(std::make_unique<HeadlessPlane>(controller, styles_));
  // Controller-level commands that name something it doesn't know itself
  // (shared UI commands like "toggle-playing") land here.
  controller->setCommandFallback([this](std::string_view name) { return executeCommand(name); });
}

void
HeadlessUI::setStatus(std::string s) {
  using namespace std::chrono;
  auto now = system_clock::now();
  auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
  time_t t = system_clock::to_time_t(now);
  struct tm tm_buf;
  localtime_r(&t, &tm_buf);
  char stamp[16];
  strftime(stamp, sizeof stamp, "%H:%M:%S", &tm_buf);
  fprintf(stderr, "[%s.%03d] %s\n", stamp, static_cast<int>(ms), s.c_str());
  fflush(stderr);
}

void
HeadlessUI::handleLogEvent(LogEvent & ev) {
  setStatus(ev.getText());
}

void
HeadlessUI::handleMidiEvent(MidiEvent & ev) {
  auto & controller = getController();
  auto & song = controller.getSong();
  auto & queue = controller.getPlaybackEventQueue();
  int track_id = song.getCurrentTrackId();
  auto buffer = controller.getActiveBufferName();

  if (ev.getType() == MidiEvent::CHANNEL_PRESSURE) {
    queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CHANNEL_PRESSURE, buffer, track_id, ev.getVelocity()));
    return;
  }

  // The nearest note of the song's tuning to the 12-EDO pitch received.
  int note_value = 0;
  if (song.getTuning() == Tuning::TET12) {
    note_value = ev.getNote();
  } else {
    float best_diff = 1e6f, f = getFrequencyFor(Tuning::TET12, ev.getNote());
    for (int i = 0; i < 255; i++) {
      float diff = fabsf(f - getFrequencyFor(song.getTuning(), i));
      if (diff < best_diff) {
	note_value = i;
	best_diff = diff;
      }
    }
  }

  // Each held note gets the lowest free voice slot, the way chords land
  // in separate note columns.
  auto it = active_midi_notes_.find(ev.getNote());
  int note_column;
  if (it != active_midi_notes_.end()) {
    note_column = it->second;
  } else {
    note_column = 0;
    auto taken = [&](int c) {
      return any_of(active_midi_notes_.begin(), active_midi_notes_.end(), [c](auto & kv) { return kv.second == c; });
    };
    while (taken(note_column)) note_column++;
  }

  bool is_off = ev.getType() == MidiEvent::NOTE_OFF || (ev.getType() == MidiEvent::NOTE_ON && ev.getVelocity() == 0);
  if (is_off) {
    if (it == active_midi_notes_.end()) return;
    active_midi_notes_.erase(it);
    queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, buffer, track_id, note_column));
  } else if (ev.getType() == MidiEvent::NOTE_ON) {
    // A repeated note-on for a held note retriggers the same voice slot.
    active_midi_notes_[ev.getNote()] = note_column;
    if (controller.isMonitoring(track_id)) {
      queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, buffer, track_id, note_column, note_value, ev.getVelocity()));
    }
  } else if (ev.getType() == MidiEvent::NOTE_PRESSURE && it != active_midi_notes_.end()) {
    queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::NOTE_PRESSURE, buffer, track_id, note_column, note_value, ev.getVelocity()));
  }
}

void
HeadlessUI::handlePlaybackEvent(PlaybackEvent & ev) {
  getController().receivePlaybackSnapshot(ev.getBufferName(), ev.getInfo());
  if (ev.getBufferName() != getController().getActiveBufferName()) return;

  bool playing = getController().getPlaybackInfo().isPlaying();
  if (playing != was_playing_) {
    was_playing_ = playing;
    setStatus(playing ? "Playing" : "Stopped");
  }

  // Same ordering requirement as the terminal backend: row-advance work
  // runs right after the snapshot lands, before any pad press reads it.
  if (launchpad_manager_) {
    launchpad_manager_->onRowAdvanced(getController());
    getController().extendRecordingClipsIfNeeded(launchpad_manager_->getAutoRecordClipIds(), launchpad_manager_->getActiveNoteTrackIds());
  }
  getController().extendRecordingSampleClipIfNeeded();
}

void
HeadlessUI::wireLaunchpad(LaunchpadManager & launchpad_manager) {
  // No overview bar cursor to move here.
  launchpad_manager.setSessionMoveBarCallback([](int) { });
  // The shared track cursor is just Song's current track.
  launchpad_manager.setTrackMoveCallback([this](int new_track_index) {
    auto & song = getController().getSong();
    auto track_ids = song.getPlayableTrackIds();
    if (new_track_index >= 0 && new_track_index < static_cast<int>(track_ids.size())) {
      song.setCurrentTrackId(track_ids[static_cast<size_t>(new_track_index)]);
    }
  });
  getController().getSessionPlayer().setAssignPlaybackStarter([this]() { launchpad_manager_->startAssignPlayback(getController()); });
  getController().setDrumEditRequestListener([this](int track_id, bool opened) {
    if (opened) {
      getController().getSong().setCurrentTrackId(track_id);
      launchpad_manager_->forceNotesModeOnAllDevices();
      launchpad_manager_->resetStepGridView();
      if (getController().getPlaybackInfo().isPlaying()) getController().togglePlaying();
      getController().getSessionPlayer().silenceAll();
    } else {
      launchpad_manager_->forceSessionModeOnAllDevices();
    }
  });
}

void
HeadlessUI::tick() {
  auto & controller = getController();
  auto & song = controller.getSong();
  controller.syncMonitoring();
  controller.getSessionPlayer().tick();

  if (launchpad_manager_) {
    auto track_ids = song.getPlayableTrackIds();
    LaunchpadManager::SessionWindow session;
    session.track_ids = track_ids;
    launchpad_manager_->refresh(song, track_ids, controller.getPlaybackInfo(),
      track_ids.empty() ? -1 : indexOfTrack(track_ids, song.getCurrentTrackId()), controller, session);
  }
}

void
HeadlessUI::startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) {
  auto midi_descriptors = audio.getMidiCaptureDescriptors();
  auto launchpad_descriptors = launchpad_io.getPollDescriptors();
  constexpr size_t kMidiBase = 1;
  size_t launchpad_base = kMidiBase + midi_descriptors.size();
  vector<pollfd> descriptors(launchpad_base + launchpad_descriptors.size());

  descriptors[0].fd = getController().getUIEventQueue().getPollFd();
  descriptors[0].events = POLLIN;
  copy(midi_descriptors.begin(), midi_descriptors.end(), descriptors.begin() + kMidiBase);
  copy(launchpad_descriptors.begin(), launchpad_descriptors.end(), descriptors.begin() + static_cast<ptrdiff_t>(launchpad_base));

  if (autoplay_) {
    getController().togglePlaying();
    setStatus("Autoplay");
  }

  while (!shouldClose()) {
    // poll() returns early with EINTR when a shutdown signal arrives.
    int poll_result = poll(descriptors.data(), descriptors.size(), kTickIntervalMs);
    if (poll_result > 0) {
      for (size_t i = 0; i < descriptors.size(); i++) {
	if (!descriptors[i].revents) continue;
	if (i == 0) {
	  while (getController().getUIEventQueue().hasEvents()) {
	    auto event = getController().getUIEventQueue().pop();
	    handleEvent(*event);
	  }
	} else if (i < launchpad_base) {
	  for (auto & ev : audio.recordMIDI()) handleEvent(ev);
	} else {
	  for (auto & ev : launchpad_io.pollEvents()) handleEvent(*ev);
	}
      }
    }
    tick();
  }
}
