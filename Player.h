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
  Player(ChannelConfiguration channel_config, Controller * controller)
    : state_(channel_config), controller_(controller) { }

  void handlePlaybackControlEvent(PlaybackControlEvent & ev) override;

  void play(AudioAPI & audio);
  std::unique_ptr<PlaybackEvent> createPlaybackEvent(const Song & song, SongState & state);

private:
  std::unique_ptr<Mixer> createMixer(short outChannels, int outSampleRate, MixerType type);
  
  SongState state_;
  Controller * controller_;
  bool terminate_ = false;
};

#endif
