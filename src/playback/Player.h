#ifndef _PLAYER_H_
#define _PLAYER_H_

#include "EventHandler.h"
#include "../state/SongState.h"
#include "../state/VoiceState.h"
#include "../ambisonic/MixerType.h"
#include "../dsp/RecordingRingBuffer.h"
#include "../model/GroovePatternLibrary.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Controller;
class AudioAPI;
class Song;
class Instrument;

class Player : public EventHandler {
 public:
  Player(ChannelConfiguration channel_config, Controller * controller)
    : channel_config_(channel_config), controller_(controller),
      threshold_ring_buffer_(static_cast<int>(kThresholdRingBufferSeconds * channel_config.getAudioOutSampleRate())) { }

  void handlePlaybackControlEvent(PlaybackControlEvent & ev) override;

  // Test-only introspection - the actual real-time render loop (play())
  // never calls this. -1 when `name` has no live SongState (whether it's
  // never been touched or only has a pending_positions_ entry); otherwise
  // that state's own current absolute row.
  int getLiveStatePosition(const std::string & name) const {
    auto it = live_states_.find(name);
    return it == live_states_.end() ? -1 : it->second->getAbsolutePosition();
  }

  // Same shape as getLiveStatePosition() above, for that state's own
  // getPositionEditSeq() - what Controller::receivePlaybackSnapshot()
  // actually compares against its own per-buffer local_position_edit_seq_
  // to detect a stale snapshot (see Controller.h's own comment).
  int getLiveStatePositionEditSeq(const std::string & name) const {
    auto it = live_states_.find(name);
    return it == live_states_.end() ? -1 : it->second->getPositionEditSeq();
  }

  // Test-only, same "not used by play() itself" caveat as the two above -
  // lets a test drive a buffer's own live SongState through further
  // renderBlock() calls directly (voice release/reclaim needs real render
  // blocks to progress, not just the note-on/off event that starts it),
  // something neither getLiveStatePosition() nor getLiveStatePositionEditSeq()
  // exposes a way to do. nullptr when `name` has no live SongState yet.
  SongState * getLiveStateForTest(const std::string & name) {
    auto it = live_states_.find(name);
    return it == live_states_.end() ? nullptr : it->second.get();
  }

  void play(AudioAPI & audio);
  std::unique_ptr<PlaybackEvent> createPlaybackEvent(const std::string & buffer_name, const Song & song, const SongState & state);

  // OutlineView's own audition path - both a single Library-instrument
  // note (PlaybackControlEvent::PREVIEW_NOTE/PREVIEW_STOP) and a whole
  // looping Library groove pattern (PREVIEW_GROOVE/PREVIEW_STOP) render
  // through here, combined into one buffer and mixed into the shared
  // Mixer every block by play()'s own poll loop, right alongside every
  // live buffer's own SongState (see that call site). Public, not private,
  // so a test can drive it directly the same way getLiveStateForTest()
  // lets a test drive a real buffer's SongState - play() itself is the
  // only other caller. Reclaims each voice once its release tail finishes
  // (VoiceState::isActive() false), the same as InstrumentTrackState::
  // clearFinishedVoices() does for a real track's own voices; returns a
  // zero-channel, correctly frame-sized AudioBuffer while nothing is
  // previewing, safe to Mixer::accumulate() unconditionally either way.
  AudioBuffer renderPreview(int frames);

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

  // Target row (and how many MOVE_POSITION/SET_POSITION events have
  // contributed to it) for a buffer that has no live_states_ entry yet -
  // see handlePlaybackControlEvent()'s own comment on why those two event
  // types deliberately never call stateFor(). Row navigation while
  // stopped is not a sound-producing event, so it must not be what gives
  // a buffer its permanent, forever-rendered SongState - the per-buffer
  // editing/playback-state plan's own stated intent, which this restores
  // (a real gap between that intent and what the code actually did, not
  // just a theoretical one - see MOVE_POSITION's own comment). Applied to
  // the real SongState the moment stateFor() actually constructs one for
  // this buffer (via some later, genuinely sound-producing event), so
  // "hit Play after moving the cursor around in a buffer that's never
  // made a sound yet" still starts from the right row. `edit_seq` counts
  // every such event while stateless, not just the latest one - stamped
  // onto the freshly-constructed SongState's own getPositionEditSeq()
  // (SongState::setPositionWithEditSeq(), not the plain setPosition() a
  // live buffer's own row navigation uses) so it starts already
  // reflecting every edit Controller's own per-buffer edit counter
  // (Controller::local_position_edit_seq_) already knows about, instead
  // of always resetting to 1 regardless of how many navigation events
  // actually preceded the buffer's first sound - confirmed as a real
  // regression (a buffer navigated many times before ever being played
  // showed a frozen playhead/info line once playback started, since
  // Controller's own edit counter had already run far ahead of the
  // freshly-built SongState's).
  struct PendingPosition { int row; int edit_seq; };
  std::unordered_map<std::string, PendingPosition> pending_positions_;

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
  // play()'s own poll loop - the previous iteration's Controller::
  // isRecording(), compared against the current one to detect recording
  // actually engaging (see play()'s own comment on why this is where the
  // round-trip latency measurement happens, exactly once per take).
  bool was_recording_ = false;

  // Loudness-threshold-armed recording (Controller::isThresholdArmed()) -
  // audio-thread-only state, the same ownership boundary was_recording_/
  // the latency measurement above already established for this class's
  // own cross-thread concerns.
  static constexpr float kThresholdRingBufferSeconds = 0.5f;
  // -40dB: comfortably above a quiet room's own noise floor, comfortably
  // below a real, deliberate attack - a starting point tuned by ear, not
  // measured, the same "no manual/user-configurable constant yet" scope
  // the round-trip latency figure above already accepted for a comparable
  // concern (unlike that one, though, this is genuinely performance/
  // source-dependent, so a future per-song/per-take knob is a plausible
  // low-risk follow-up once this ships and gets used for real).
  static constexpr float kThresholdRecordTriggerDB = -40.0f;
  // Sized in the constructor's own initializer list (from the constructor
  // parameter, not channel_config_ - a plain in-class default here would
  // depend on channel_config_ already being constructed purely by
  // declaration-order accident, fragile against a future reordering of
  // this class's own members).
  RecordingRingBuffer threshold_ring_buffer_;
  // The previous iteration's Controller::isThresholdArmed(), same
  // false->true/true->false edge-detection idiom as was_recording_ above -
  // a fresh arm (false->true) resets threshold_ring_buffer_ so an earlier,
  // temporally-discontinuous arm cycle's own leftover content can never
  // bleed into a later one's own drain().
  bool was_threshold_armed_ = false;
  // Latched true the instant this arm cycle's own trigger actually fires,
  // cleared on the next false->true edge above - without it, every
  // capture block still above threshold after the first one would fire
  // its own duplicate trigger, since Controller::isThresholdArmed() won't
  // actually flip false until the UI thread processes the resulting event
  // a few iterations later.
  bool threshold_triggered_this_arm_cycle_ = false;

  // Stands in for a live PLAY_NOTE's own NoteCoordinate absolute_row (see
  // handlePlaybackControlEvent()'s own comment) - a live note has no
  // authored song position to build a real one from. Deliberately
  // process-lifetime monotonic, never reset: live performance was never a
  // reproducibility target in the first place (see NoteCoordinate.h), so
  // this only needs to keep successive/simultaneous live notes decorrelated
  // from each other, not to reproduce any particular value run to run.
  int live_note_counter_ = 0;

  // OutlineView's own instrument-audition path - a single ad hoc
  // VoiceState with no owning Track/buffer at all, unlike every other
  // note-producing event, which always resolves through
  // stateFor()/live_states_ above. Retriggering (a fresh PREVIEW_NOTE
  // while one is already sounding) just replaces it outright - an
  // instrument browser has no need for the real polyphony/release-tail
  // bookkeeping InstrumentTrackState gives a real track's own notes. See
  // renderPreview() above and PlaybackControlEvent::PREVIEW_NOTE/
  // PREVIEW_STOP's own doc comment.
  std::unique_ptr<VoiceState> preview_note_voice_;

  // OutlineView's own groove-pattern (Library > Grooves) preview path - a
  // pointer into GroovePatternLibrary.h's own function-local static table
  // (valid for the life of the process, see getGroovePatternLibrary()'s
  // own doc comment), null while nothing is previewing. Unlike
  // preview_note_voice_ above, a groove genuinely needs real, concurrent
  // polyphony (a kick and a hi-hat landing on the same step are two
  // simultaneous voices, not one replacing the other) and a real
  // scheduler advancing it block by block (renderPreview()'s own body) -
  // there is no per-buffer SongState to lean on here, since the groove
  // isn't attached to any song/track at all until "Add to Song" actually
  // creates one.
  const GroovePatternTemplate * preview_groove_pattern_ = nullptr;
  // Resolved once, when PREVIEW_GROOVE starts (handlePlaybackControlEvent()),
  // not re-resolved every block - the same "kit" a real PercussionTrack's
  // own default kit resolves to (InstrumentPool::prepare()'s own
  // GenericInstrument, literal/path lookup falling back to the provider's
  // generic default instrument), reused here via a throwaway
  // GenericInstrument rather than duplicating that fallback chain inline.
  // Null exactly when preview_groove_pattern_ is (both set together).
  std::shared_ptr<Instrument> preview_groove_instrument_;
  // Current position within preview_groove_pattern_'s own loop, in
  // frames at the active song's tempo (renderPreview() re-derives the
  // loop's total frame length from this every block, via
  // ChannelConfiguration::getSampleInterval() - cheap, and correctly
  // reacts to a live tempo change mid-preview rather than latching a
  // stale one). Always kept within [0, loop length) - meaningless while
  // preview_groove_pattern_ is null.
  int preview_groove_frame_ = 0;
  // Every currently-sounding hit from preview_groove_pattern_ - see its
  // own comment on why this needs real polyphony, unlike
  // preview_note_voice_.
  std::vector<std::unique_ptr<VoiceState>> preview_groove_voices_;
};

#endif
