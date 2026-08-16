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
  std::unique_ptr<PlaybackEvent> createPlaybackEvent(const Song & song, const SongState & state);

private:
  SongState state_;
  Controller * controller_;
  bool terminate_ = false;
  bool song_changed_ = false;
  bool mixer_changed_ = false;

  // Stands in for a live PLAY_NOTE's own NoteCoordinate absolute_row (see
  // handlePlaybackControlEvent()'s own comment) - a live note has no
  // authored song position to build a real one from. Deliberately
  // process-lifetime monotonic, never reset: live performance was never a
  // reproducibility target in the first place (see NoteCoordinate.h), so
  // this only needs to keep successive/simultaneous live notes decorrelated
  // from each other, not to reproduce any particular value run to run.
  int live_note_counter_ = 0;
};

#endif
