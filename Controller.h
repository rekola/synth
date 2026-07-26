
#ifndef _CONTROLLER_H_
#define _CONTROLLER_H_

#include "SampleData.h"
#include "InstrumentProvider.h"
#include "EventQueue.h"
#include "PlaybackInfo.h"
#include "ChannelConfiguration.h"
#include "MixerType.h"

#include <functional>
#include <memory>
#include <string>

class Song;

class Controller {
 public:
  Controller(ChannelConfiguration _channel_config);

  const Song & getSong() const { return *current_song; }
  Song & getSong() { return *current_song; }

  const std::string & getSongFilename() const { return current_song_filename; }

  void createNewSong();
  bool openSong(const std::string & filename);
  
  void loadDemo2();
  bool sendCommand(std::string_view s);

  // Lets the UI layer (which Controller, part of the headless-testable
  // synth_engine lib, must not depend on) supply a fallback for command
  // names sendCommand() doesn't recognize itself - e.g. per-widget Emacs
  // commands like "set-mark" that live in a UIElement's CommandRegistry.
  void setCommandFallback(std::function<bool(std::string_view)> fn) { command_fallback_ = std::move(fn); }

  std::shared_ptr<SampleData> startRecording() {
    current_sample = std::make_shared<SampleData>(1, 0);
    return current_sample;
  }

  void stopRecording() { current_sample.reset(); }
  bool isRecording() const { return current_sample.get() != nullptr; }
  int getRecordingTrackId() const { return recording_track_id; }
  void setRecordingTrackId(int track_id) { recording_track_id = track_id; }
  const SampleData & getCurrentSample() const { return current_sample ? *current_sample : empty_sample; }
  void addToSample(const SampleData & other) {
    if (current_sample) current_sample->append(other);
  }

  EventQueue & getUIEventQueue() { return ui_event_queue; }
  EventQueue & getPlaybackEventQueue() { return playback_event_queue; }

  void setPlaybackInfo(const PlaybackInfo & info) { playback_info = info; }
  const PlaybackInfo & getPlaybackInfo() const { return playback_info; }

  ChannelConfiguration getChannelConfiguration() const { return channel_config; }

  // Process-wide decoder choice, independent of any particular song (any
  // song can be rendered through any decoder - see MixerType.h). Only
  // meaningful when getChannelConfiguration().getType() == AMBISONIC - a
  // MONO config never attempts binaural decoding regardless of this
  // setting (see MixerFactory.cpp).
  MixerType getMixerType() const { return mixer_type_; }
  void setMixerType(MixerType mixer_type) { mixer_type_ = mixer_type; }

  // AMBISONIC_BINAURAL has two underlying implementations - MagLS
  // (AmbisonicMagLSDecoder, the default) and the older virtual-speaker
  // rig (AmbisonicBinauralMixer) - without a third MixerType value for it
  // (see MixerType.h's own comment on why): this is a separate, orthogonal
  // toggle, same shape as --stereo's own force_cardioid, reachable via
  // --legacy-binaural (main.cpp). Ignored entirely unless mixer_type_ is
  // actually AMBISONIC_BINAURAL (see MixerFactory.cpp).
  bool getUseLegacyBinaural() const { return use_legacy_binaural_; }
  void setUseLegacyBinaural(bool use_legacy) { use_legacy_binaural_ = use_legacy; }

  bool togglePlaying();

  // Single, shared home for "mutate this track's mute/solo/send and keep
  // the already-running playback state in sync" - neither the terminal's
  // `\` key handler nor any Launchpad control (the two ways a user can
  // trigger these today) duplicate this logic; both just resolve which
  // track_id to act on (whichever way is natural for that input source -
  // the shared on-screen cursor, or a Launchpad device's own assigned
  // track) and call these. Returns false (Send setters: no-op) if track_id
  // doesn't name an existing InstrumentTrack/PercussionTrack. Each also
  // pushes the matching PlaybackControlEvent so the change actually reaches
  // the running SongState, not just the Track model - see
  // InstrumentTrackState's public setMuted/setSolo/setSendA/setSendB/
  // setSendMain.
  bool toggleTrackMuted(int track_id);
  bool toggleTrackSolo(int track_id);
  void setTrackSendA(int track_id, float value);
  void setTrackSendB(int track_id, float value);
  void setTrackSendMain(int track_id, float value);
  void setTrackAzimuth(int track_id, float value);

  // Emacs prefix-argument style: transient, one-shot context a caller (the
  // Launchpad command-dispatch path, UI::handleLaunchpadButtonEvent) sets
  // right before invoking a named command by string (executeCommand()),
  // for whichever registered command actually wants it (currently
  // "toggle-mute"/"toggle-solo", resolving which track a specific
  // Launchpad device is following - see PatternEditor's constructor) - the
  // caller doesn't need to know which commands care, and a command that
  // doesn't consume it simply leaves it to be overwritten/cleared by the
  // next dispatch. consumePendingCommandTrack() reads and clears in one
  // step, exactly like reading Emacs's current-prefix-arg resets it - so a
  // stale value can never leak into a later, unrelated command (e.g. one
  // invoked from a keybinding or M-x, which never sets this at all and
  // always gets the caller-supplied fallback instead).
  void setPendingCommandTrack(int track_id) { pending_command_track_ = track_id; }
  int consumePendingCommandTrack(int fallback) {
    if (pending_command_track_ < 0) return fallback;
    int track_id = pending_command_track_;
    pending_command_track_ = -1;
    return track_id;
  }

  const InstrumentProvider & getInstrumentProvider() const { return instrument_provider; }

 private:
  ChannelConfiguration channel_config;
  MixerType mixer_type_ = MixerType::AMBISONIC_STEREO;
  bool use_legacy_binaural_ = false;

  std::shared_ptr<Song> current_song;
  std::string current_song_filename = "song.xml";
  std::shared_ptr<SampleData> current_sample;
  InstrumentProvider instrument_provider;
  EventQueue ui_event_queue, playback_event_queue;
  PlaybackInfo playback_info;
  int recording_track_id = 0;
  std::function<bool(std::string_view)> command_fallback_;
  int pending_command_track_ = -1;

  static inline SampleData empty_sample;
};

#endif
