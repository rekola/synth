#ifndef _PLAYER_H_
#define _PLAYER_H_

#include "EventHandler.h"
#include "SongState.h"

#include <memory>

class Controller;
class AudioAPI;
class SongState;
class EventQueue;
class Song;

class Player : public EventHandler {
 public:
  Player(unsigned int _outSampleRate, Controller * _controller)
    : outSampleRate(_outSampleRate), controller(_controller), state(_outSampleRate) { }

  void handlePlaybackControlEvent(PlaybackControlEvent & ev) override;

  void play(AudioAPI & audio);
  std::unique_ptr<PlaybackEvent> createPlaybackEvent(const Song & song, SongState & state);

private:
  unsigned int outSampleRate;
  Controller * controller;
  SongState state;
};

#endif
