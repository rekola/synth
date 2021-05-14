#ifndef _CONTROLLER_H_
#define _CONTROLLER_H_

#include "SampleData.h"
#include "InstrumentProvider.h"
#include "EventQueue.h"
#include "PlaybackInfo.h"

#include <memory>

class Song;

class Controller {
 public:
  Controller();

  const Song & getSong() const { return *current_song; }
  Song & getSong() { return *current_song; }

  void createNewSong();
  void openSong(std::string filename);
  
  void loadDemo();
  void loadDemo2();
  void loadDemo3();
  void loadDemo7();
  bool sendCommand(const std::string & s);

  std::shared_ptr<SampleData> startRecording() {
    current_sample = std::make_shared<SampleData>(1, 0);
    return current_sample;
  }

  void stopRecording() { current_sample.reset(); }
  bool isRecording() const { return current_sample.get() != nullptr; }
  const SampleData & getCurrentSample() const { return current_sample ? *current_sample : empty_sample; }
  void addToSample(const SampleData & other) {
    if (current_sample) current_sample->append(other);
  }

  EventQueue & getEventQueue() { return event_queue; }

  void setPlaybackInfo(const PlaybackInfo & info) { playback_info = info; }
  const PlaybackInfo & getPlaybackInfo() const { return playback_info; }
  
 private:
  std::shared_ptr<Song> current_song;
  std::shared_ptr<SampleData> current_sample;
  SampleData empty_sample;
  InstrumentProvider instrument_provider;
  EventQueue event_queue;
  PlaybackInfo playback_info;
};

#endif
