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
  // Switches to (opening the first time) the SessionView/OutlineView/
  // PatternEditor aspect of the active song - see Controller::
  // openSessionViewBuffer()'s own comment. Any of the three can be opened
  // regardless of which one currently shows, and each can later be closed
  // independently (kill-buffer) without closing the song.
  commands_.define("session-view", [this]() {
    getController().openSessionViewBuffer();
  });
  commands_.define("outline-view", [this]() {
    getController().openOutlineViewBuffer();
  });
  commands_.define("pattern-viewer", [this]() {
    getController().openPatternEditorBuffer();
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
}

void
StatusLogger::log(std::string s) {
  ui_->setStatus(std::move(s));
}
