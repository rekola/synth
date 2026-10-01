#ifndef _HEADLESSUI_H_
#define _HEADLESSUI_H_

#include "../UI.h"
#include "../StyleProvider.h"

#include <memory>
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

  void handlePlaybackEvent(PlaybackEvent & ev) override;
  void handleLogEvent(LogEvent & ev) override;
  // Plays incoming MIDI live on the current track. Unlike the terminal
  // UI there is no edit cursor, so notes are not written into patterns.
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
  // MIDI note number -> note column (voice slot) of a sounding note.
  std::unordered_map<int, int> active_midi_notes_;
};

#endif
