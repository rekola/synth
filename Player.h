#ifndef _PLAYER_H_
#define _PLAYER_H_

#include "EventHandler.h"
#include "SongState.h"
#include "MixerType.h"
#include "FFT.h"

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
  std::unique_ptr<PlaybackEvent> createPlaybackEvent(const Song & song, const SongState & state);

private:
  SongState state_;
  Controller * controller_;
  bool terminate_ = false;
  bool song_changed_ = false;
  bool mixer_changed_ = false;
  FFT fft_;
};

#endif
