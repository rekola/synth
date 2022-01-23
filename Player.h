#ifndef _PLAYER_H_
#define _PLAYER_H_

#include "EventHandler.h"
#include "SongState.h"
#include "MixerType.h"

#include <memory>

class Controller;
class AudioAPI;
class Song;

class Player : public EventHandler {
 public:
  Player(ChannelConfiguration _channel_config, unsigned int _outSampleRate, Controller * _controller)
    : outSampleRate(_outSampleRate), controller(_controller), state(_channel_config, _outSampleRate) { }

  void handlePlaybackControlEvent(PlaybackControlEvent & ev) override;

  void play(AudioAPI & audio);
  std::unique_ptr<PlaybackEvent> createPlaybackEvent(const Song & song, SongState & state);

private:
  std::unique_ptr<Mixer> createMixer(unsigned int outChannels, MixerType type);
  
  unsigned int outSampleRate;
  Controller * controller;
  SongState state;
  bool terminate = false;
};

#endif
