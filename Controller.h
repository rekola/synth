#ifndef _CONTROLLER_H_
#define _CONTROLLER_H_

#include <memory>

class Song;
class Synth;

class Controller {
 public:
  Controller();

  const Song & getSong() const { return *current_song; }
  Song & getSong() { return *current_song; }

  const Synth & getSynth() const { return *synth; }
  Synth & getSynth() { return *synth; }

  void setSynth(std::shared_ptr<Synth> & _synth) { synth = _synth; }

  void createNewSong();
  bool sendCommand(const std::string & s);
  
 private:
  std::shared_ptr<Synth> synth;
  std::shared_ptr<Song> current_song;
};

#endif
