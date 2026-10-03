#ifndef _HEADLESSUI_H_
#define _HEADLESSUI_H_

#include "../UI.h"
#include "../StyleProvider.h"

#include <memory>
#include "../../playback/MidiNoteInput.h"

#include <string>
#include <unordered_map>
#include <string>

// UI backend with no screen or keyboard: plays the song and keeps the
// Launchpad working, reporting status as timestamped lines on stderr.
class HeadlessUI : public UI {
 public:
  HeadlessUI() = default;

  void initialize(std::shared_ptr<Controller> & controller);

  // Start the transport as soon as the main loop is running.
  void setAutoplay(bool autoplay) { autoplay_ = autoplay; }

  void refresh() override { }
  void render() override { }
  void setStatus(std::string s) override;
  // No dialogs here: the content goes to the log.
  void showInfoDialog(const std::string & title, const std::string & markdown) override;

  void handlePlaybackEvent(PlaybackEvent & ev) override;
  void handleLogEvent(LogEvent & ev) override;
  // Plays incoming MIDI live on the current track; while note capture is
  // armed and the transport plays, also records it at the playhead.
  void handleMidiEvent(MidiEvent & ev) override;

protected:
  void startUI(AudioAPI & audio, LaunchpadIO & launchpad_io) override;
  void wireLaunchpad(LaunchpadManager & launchpad_manager) override;

private:
  // The once-per-frame work a visual backend does in renderComponents().
  void tick();

  StyleProvider styles_;
  bool autoplay_ = false;
  bool was_playing_ = false;
  MidiNoteInput midi_input_;
  // Clips this UI's own MIDI takes created (Controller::ensureNoteRecordingClip()).
  std::unordered_map<int, std::string> midi_record_clip_ids_;
};

#endif
