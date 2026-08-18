#ifndef _PLAYER_H_
#define _PLAYER_H_

#include "EventHandler.h"
#include "../state/SongState.h"
#include "../ambisonic/MixerType.h"

#include <memory>
#include <string>
#include <unordered_map>

class Controller;
class AudioAPI;
class Song;

class Player : public EventHandler {
 public:
  Player(ChannelConfiguration channel_config, Controller * controller)
    : channel_config_(channel_config), controller_(controller) { }

  void handlePlaybackControlEvent(PlaybackControlEvent & ev) override;

  void play(AudioAPI & audio);
  std::unique_ptr<PlaybackEvent> createPlaybackEvent(const std::string & buffer_name, const Song & song, const SongState & state);

private:
  // One live SongState per buffer that's actually made sound (see the
  // per-buffer editing/playback-state plan's Part B) - not one per open
  // buffer (a buffer merely opened or switched to but never actually
  // played/auditioned/edited has no entry at all, see stateFor()'s own
  // comment), and no longer a single global one either. Every entry here
  // is rendered and accumulated into the shared Mixer every block,
  // unconditionally; only playing_buffer_name_ below says which one's own
  // pattern-scheduler is allowed to auto-advance. Kept alive for as long
  // as the buffer stays open (never torn down just because it stops being
  // active or playing - a release tail must keep sounding), dropped only
  // on BUFFER_KILLED.
  std::unordered_map<std::string, std::unique_ptr<SongState>> live_states_;

  // Which live_states_ entry (empty when nothing is playing) actually has
  // its pattern position auto-advanced each block. A single name, not a
  // per-SongState flag each one carries independently - "at most one
  // playing buffer" is then true by construction (there's exactly one
  // name to compare against), not something that has to be kept
  // consistent across N separate flags (see the plan's own reasoning).
  std::string playing_buffer_name_;

  // Get-or-creates buffer `name`'s own live SongState against `song`,
  // constructing and initializing a fresh one the first time any event
  // ever actually targets this buffer - see PlaybackControlEvent::
  // getBufferName(). This is deliberately the *only* place a SongState
  // gets constructed: merely switching which buffer is active in the UI
  // never reaches Player at all any more (no event fires), so a buffer
  // opened but never played/auditioned/edited stays with no live SongState
  // at all, however many such buffers are open.
  SongState & stateFor(const std::string & name, const Song & song);

  ChannelConfiguration channel_config_;
  Controller * controller_;
  bool terminate_ = false;
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
