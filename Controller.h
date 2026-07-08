
#ifndef _CONTROLLER_H_
#define _CONTROLLER_H_

#include "SampleData.h"
#include "InstrumentProvider.h"
#include "EventQueue.h"
#include "PlaybackInfo.h"
#include "ChannelConfiguration.h"

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

  bool togglePlaying();

  const InstrumentProvider & getInstrumentProvider() const { return instrument_provider; }
  
 private:
  ChannelConfiguration channel_config;

  std::shared_ptr<Song> current_song;
  std::string current_song_filename = "song.xml";
  std::shared_ptr<SampleData> current_sample;
  InstrumentProvider instrument_provider;
  EventQueue ui_event_queue, playback_event_queue;
  PlaybackInfo playback_info;
  int recording_track_id = 0;

  static inline SampleData empty_sample;
};

#endif
