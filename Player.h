#ifndef _PLAYER_H_
#define _PLAYER_H_

#include <memory>

class Logger;
class Controller;
class AudioAPI;
class SongState;
class EventQueue;
class Song;
class PlaybackEvent;

class Player {
 public:
  Player() { }

  void play(Logger & logger, Controller & controller, AudioAPI & audio);
  std::unique_ptr<PlaybackEvent> createPlaybackEvent(const Song & song, SongState & state);
};

#endif
