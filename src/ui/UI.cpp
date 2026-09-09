#include "UI.h"

#include "../audio/AudioAPI.h"
#include "../audio/AudioBuffer.h"
#include "../launchpad/LaunchpadIO.h"
#include "../launchpad/LaunchpadManager.h"
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
StatusLogger::log(std::string s) {
  ui_->setStatus(std::move(s));
}
