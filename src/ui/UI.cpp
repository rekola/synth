#include "UI.h"

#include "../audio/AudioAPI.h"
#include "../audio/AudioBuffer.h"
#include "../launchpad/LaunchpadIO.h"
#include "../launchpad/LaunchpadManager.h"
#include "../launchpad/LaunchpadChannelPressureEvent.h"
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

#include <algorithm>
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

void
UI::start(AudioAPI & audio, LaunchpadIO & launchpad_io, LaunchpadManager & launchpad_manager) {
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
UI::initializeCommands() {
  // Every command defined here has no widget-specific dependency (a plain
  // Controller call, or setStatus() - already virtual) - shared between
  // every UI backend, so each backend's own keybindings (Emacs-style
  // chords here, whatever a future GUI uses there) just need to bind a key
  // to one of these names rather than redefine what it does. A command
  // that does need a backend-specific dialog/prompt, or reaches into a
  // concrete widget, stays defined in that backend instead (e.g.
  // TerminalUI::initializeWidgets()).
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
  // Starts with no lanes - an ordinary percussion track; apply-preset-*
  // is how it picks a kit.
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

  // Replaces the current track's whole lane list with a named preset
  // (PercussionTrack::applyPreset()'s own comment on why it replaces
  // rather than adds) - a no-op on anything but a PercussionTrack.
  auto apply_preset = [this](PercussionTrack::Preset preset) {
    auto & song = getController().getSong();
    auto track = song.getMasterTrack().getChildByInternalId(song.getCurrentTrackId());
    if (!track || track->getType() != TrackType::PERCUSSION_CONTROL) return;
    static_cast<PercussionTrack &>(*track).applyPreset(preset, song);
    song.incVersion();
  };
  commands_.define("apply-preset-rock", [apply_preset]() { apply_preset(PercussionTrack::Preset::ROCK); });
  commands_.define("apply-preset-latin", [apply_preset]() { apply_preset(PercussionTrack::Preset::LATIN); });
  commands_.define("apply-preset-electronic", [apply_preset]() { apply_preset(PercussionTrack::Preset::ELECTRONIC); });
  // The explicit way back to a plain, lane-less track.
  commands_.define("apply-preset-none", [apply_preset]() { apply_preset(PercussionTrack::Preset::NONE); });
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
