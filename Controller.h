#ifndef _CONTROLLER_H_
#define _CONTROLLER_H_

#include "SampleData.h"

#include <memory>

class Song;
class Synth;

class Controller {
 public:
  Controller() { }

  const Song & getSong() const { return *current_song; }
  Song & getSong() { return *current_song; }

  const Synth & getSynth() const { return *synth; }
  Synth & getSynth() { return *synth; }

  void setSynth(std::shared_ptr<Synth> & _synth) { synth = _synth; }
  
  void createNewSong();
  void loadDemo();
  void loadDemo2();
  void loadDemo3();
  void loadDemo4();
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
  
 private:
  std::shared_ptr<Synth> synth;
  std::shared_ptr<Song> current_song;
  std::shared_ptr<SampleData> current_sample;
  SampleData empty_sample;
};

#endif
