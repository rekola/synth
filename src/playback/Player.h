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

  // Test-only introspection - the actual real-time render loop (play())
  // never calls this. -1 when `name` has no live SongState (whether it's
  // never been touched or only has a pending_positions_ entry); otherwise
  // that state's own current absolute row.
  int getLiveStatePosition(const std::string & name) const {
    auto it = live_states_.find(name);
    return it == live_states_.end() ? -1 : it->second->getAbsolutePosition();
  }

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

  // Absolute row targeted by a MOVE_POSITION/SET_POSITION event for a
  // buffer that has no live_states_ entry yet - see
  // handlePlaybackControlEvent()'s own comment on why those two event
  // types deliberately never call stateFor(). Row navigation while
  // stopped is not a sound-producing event, so it must not be what gives
  // a buffer its permanent, forever-rendered SongState - the per-buffer
  // editing/playback-state plan's own stated intent, which this restores
  // (a real gap between that intent and what the code actually did, not
  // just a theoretical one - see MOVE_POSITION's own comment). Applied to
  // the real SongState the moment stateFor() actually constructs one for
  // this buffer (via some later, genuinely sound-producing event), so
  // "hit Play after moving the cursor around in a buffer that's never
  // made a sound yet" still starts from the right row.
  std::unordered_map<std::string, int> pending_positions_;

  // Get-or-creates buffer `name`'s own live SongState against `song`,
  // constructing and initializing a fresh one the first time any event
  // ever actually targets this buffer - see PlaybackControlEvent::
  // getBufferName(). This is deliberately the *only* place a SongState
  // gets constructed: merely switching which buffer is active in the UI
  // never reaches Player at all any more (no event fires), and neither
  // does plain row navigation while stopped (MOVE_POSITION/SET_POSITION
  // deliberately bypass this - see handlePlaybackControlEvent()'s own
  // comment and pending_positions_ above), so a buffer that's been opened
  // and even scrolled through, but never actually played or auditioned,
  // stays with no live SongState at all, however many such buffers are
  // open.
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
