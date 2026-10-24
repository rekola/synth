#include "HeadlessUI.h"

#include "../Plane.h"
#include "../../launchpad/LaunchpadIO.h"
#include "../../midi/MidiInput.h"
#include "../../util/WaitSet.h"
#include "../WaitSources.h"

#include <cellui/PlatformEventLoop.h>
#include "../../launchpad/LaunchpadManager.h"
#include "../../audio/AudioAPI.h"
#include "../../playback/LogEvent.h"
#include "../../playback/MidiEvent.h"
#include "../../playback/PlaybackControlEvent.h"
#include "../../instruments/Tuning.h"
#include "../../playback/PlaybackEvent.h"
#include "../../playback/ClipPlayer.h"
#include "../../Controller.h"
#include "../../model/ArrangementOps.h"
#include "../../model/Song.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <ctime>

using namespace std;

namespace {

// Draws nothing and takes no input; exists only because UIElement reaches
// the Controller through its plane.
class HeadlessPlane : public Plane {
 public:
  using Plane::Plane;

  using Plane::setBgColor;
  using Plane::setFgColor;
  void setFgColor(const Color &) override {}
  void setBgColor(const Color &) override {}
  void setUnderline(bool) override { }
  void setBold(bool) override { }
  void setItalic(bool) override { }
  void erase() override { }
  void putstr(int, int, const std::string &) override { }
  std::unique_ptr<Plane> createChild() override { return std::make_unique<HeadlessPlane>(getAppContext(), getStyles()); }
  void drawBorder() override { }
  bool offerInput(const cellui::Input &) override { return false; }
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

// Roughly a frame at 50 Hz - how often the clip player and Launchpad
// LEDs get serviced when no event wakes the loop sooner.
constexpr int kTickIntervalMs = 20;

} // namespace

void
HeadlessUI::initialize(std::shared_ptr<Controller> & controller) {
  setPlane(std::make_unique<HeadlessPlane>(controller, styles_));
  // Controller-level commands that name something it doesn't know itself
  // (shared UI commands like "toggle-playing") land here.
  controller->setCommandFallback([this](std::string_view name) { return executeCommand(name); });
  controller_connections_.push_back(controller->status_message.connect([this](const std::string & text) { setStatus(text); }));
}

void
HeadlessUI::setStatus(std::string s) {
  using namespace std::chrono;
  auto now = system_clock::now();
  auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
  time_t t = system_clock::to_time_t(now);
  struct tm tm_buf;
#ifdef _WIN32
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  char stamp[16];
  strftime(stamp, sizeof stamp, "%H:%M:%S", &tm_buf);
  fprintf(stderr, "[%s.%03d] %s\n", stamp, static_cast<int>(ms), s.c_str());
  fflush(stderr);
}

void
HeadlessUI::showInfoDialog(const std::string & title, const std::string & markdown) {
  setStatus(title + "\n" + markdown);
}

void HeadlessUI::showChoiceDialog(const std::string & title, std::vector<Choice>, int, std::function<void(int)>) {
  setStatus(title + ": choosing needs the terminal UI (see --list-devices)");
}

void
HeadlessUI::handleMidiEvent(MidiEvent & ev) {
  auto & controller = getController();
  auto & song = controller.getSong();
  int track_id = song.getCurrentTrackId();

  // The same rule as a Launchpad note take: capture armed, transport
  // running; the note lands at the playhead.
  MidiNoteInput::Options options;
  options.write = controller.isNoteCaptureArmed() && controller.getPlaybackInfo().isPlaying();
  options.pressure_follows_transport = options.write;
  options.delay = controller.getPlaybackInfo().getCurrentDelay();
  midi_input_.handle(ev, controller, track_id, options, [&](int id) {
    int row = controller.getPlaybackInfo().getAbsolutePosition();
    // A live take goes into its own clip instance rather than the track's
    // background pattern.
    controller.ensureNoteRecordingClip(midi_record_clip_ids_, id, row);
    return resolveEditTarget(song, id, row, controller.getFocusedClip());
  });
}

void HeadlessUI::handlePlaybackEvent(const PlaybackEvent & ev) {
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
  getController().extendRecordingClipsIfNeeded(midi_record_clip_ids_, midi_input_.heldTrackIds());
  getController().extendRecordingSampleClipIfNeeded();
}

void
HeadlessUI::wireLaunchpad(LaunchpadManager & launchpad_manager) {
  // No overview bar cursor to move here.
  launchpad_manager.setLiveMoveBarCallback([](int) { });
  // The shared track cursor is just Song's current track.
  launchpad_manager.setTrackMoveCallback([this](int new_track_index) {
    auto & song = getController().getSong();
    auto track_ids = song.getPlayableTrackIds();
    if (new_track_index >= 0 && new_track_index < static_cast<int>(track_ids.size())) {
      song.setCurrentTrackId(track_ids[static_cast<size_t>(new_track_index)]);
    }
  });
  getController().getClipPlayer().setAssignPlaybackStarter([this]() { launchpad_manager_->startAssignPlayback(getController()); });
  controller_connections_.push_back(getController().drum_edit_requested.connect([this](int track_id, bool opened) {
    if (opened) {
      getController().getSong().setCurrentTrackId(track_id);
      launchpad_manager_->openStepView();
      if (getController().getPlaybackInfo().isPlaying()) getController().togglePlaying();
      getController().getClipPlayer().silenceAll();
    } else {
      launchpad_manager_->closeStepView();
    }
  }));
}

void
HeadlessUI::tick() {
  auto & controller = getController();
  auto & song = controller.getSong();
  controller.syncMonitoring();
  controller.tickNotePressure();
  controller.getClipPlayer().tick();

  if (launchpad_manager_) {
    auto track_ids = song.getPlayableTrackIds();
    LaunchpadManager::LiveWindow live;
    live.track_ids = track_ids;
    launchpad_manager_->refresh(song, track_ids, controller.getPlaybackInfo(),
      track_ids.empty() ? -1 : indexOfTrack(track_ids, song.getCurrentTrackId()), controller, live);
  }
}

void
HeadlessUI::startUI(AudioAPI &, MidiInput & midi, LaunchpadIO & launchpad_io) {
  WaitSet waits;
  auto events_group = waits.group();
  auto midi_group = waits.group();
  auto launchpad_group = waits.group();
  waits.add(events_group, getController().getUIEventQueue().notifier());
  midi.addTo(waits, midi_group);
  launchpad_io.addTo(waits, launchpad_group);

  addWaitGroup(loop_, waits, events_group, [this] {
    while (getController().getUIEventQueue().hasEvents()) {
      auto event = getController().getUIEventQueue().pop();
      handleEvent(*event);
    }
  });
  addWaitGroup(loop_, waits, midi_group, [&] {
    for (auto & ev : midi.readEvents()) handleEvent(ev);
  });
  addWaitGroup(loop_, waits, launchpad_group, [&] {
    for (auto & ev : launchpad_io.pollEvents()) handleEvent(*ev);
  });
  // A turn ends at least every tick (a shutdown signal ends the wait early).
  loop_.setBefore([] { return kTickIntervalMs; });
  loop_.setAfter([this](bool) {
    tick();
    if (shouldClose()) loop_.quit();
  });

  if (autoplay_) {
    getController().togglePlaying();
    setStatus("Autoplay");
  }

  if (!shouldClose()) loop_.run();
}
