#ifndef _SAMPLETRACKSTATE_H_
#define _SAMPLETRACKSTATE_H_

#include "LeafTrackState.h"
#include "RenderContext.h"

class Clip;

// The LeafTrackState (see its own doc comment) for raw sample playback -
// plays one Clip's own audio at a time (triggerClip() below) rather than
// resolving pattern-driven notes through an InstrumentPool the way
// InstrumentTrackState does. Inherits stopVoices()/stopAllVoices()/mute/
// solo/sends/position/voices_ bookkeeping from LeafTrackState instead of
// reimplementing them, but triggerClip() builds and adds its own voice
// directly (SampleTrack.cpp) rather than going through noteOn() - a raw
// sample clip needs no Instrument/Track indirection resolved for it, and
// none of InstrumentTrackState's note-column/chord/pressure machinery
// applies to it either (a SampleTrack always plays at most one clip, in
// column 0, with no pitch/identity of its own).
//
// Declared in its own header rather than kept local to SampleTrack.cpp's
// anonymous namespace (unlike those two siblings): triggerClip() is
// called from SongState.h (transport-driven playback) and Player.cpp
// (Session-view live triggering), both outside SampleTrack.cpp, so it
// needs a name those call sites can dynamic_cast to.
class SampleTrackState : public LeafTrackState {
public:
  explicit SampleTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, const SphericalPosition & position, const SendLevels & sends)
    : LeafTrackState(channel_config, solo, muted, track_id, position, sends) { }

  // Splits the block at every RenderContext::getPendingSampleEvents()
  // entry due within it (a Clip start or stop, this class's own sibling
  // timeline to InstrumentTrackState::render()'s pending_events_ - see
  // SampleTrackEvent's own comment), applies it exactly there, then keeps
  // rendering - the same chunked shape InstrumentTrackState::render() uses
  // for pattern note-on/off events, just against this track's own
  // timeline instead. See SampleTrackEvent's own comment for why a sound
  // producer should never need to do this itself.
  AudioBuffer render(int frames, const InstrumentPool &, RenderContext & context) override {
    clearFinishedVoices();

    std::vector<std::pair<int, AudioBuffer> > chunks;
    auto & pending = context.getPendingSampleEvents(getTrackId());

    for (int i = 0; i < frames; ) {
      int render_size = frames - i;
      if (!pending.empty()) {
	auto it = pending.begin();
	assert(i <= it->first);
	if (i == it->first) {
	  if (it->second.kind == SampleTrackEvent::START && it->second.clip) {
	    // context.getBpm() is exactly the song's own current tempo
	    // (SongState::initialize()'s render_context_.setBpm(tempo_)) -
	    // triggerClip()'s own tempo-stretch decision needs it in the
	    // same units SampleContent::getOriginalTempo() is authored in.
	    triggerClip(*it->second.clip, static_cast<int>(context.getBpm()), it->second.start_offset_frames);
	  } else if (it->second.kind == SampleTrackEvent::STOP) {
	    // A short natural release, not a hard cut - stopping outright
	    // here would click (SampleTrackEvent's own comment).
	    stopVoices(0);
	  }
	  it = pending.erase(it);
	}
	if (it != pending.end() && it->first - i < render_size) render_size = it->first - i;
      }
      chunks.emplace_back(i, renderVoices(render_size));
      i += render_size;
    }

    bool has_main = false, has_aux_a = false, has_aux_b = false;
    for (auto & [ pos, s ] : chunks) {
      has_main = has_main || s.hasChannel(Channel::Main);
      has_aux_a = has_aux_a || s.hasChannel(Channel::AuxA);
      has_aux_b = has_aux_b || s.hasChannel(Channel::AuxB);
    }
    AudioBuffer data(has_main ? getChannelConfiguration().numberOfChannels() : 0, has_aux_a, has_aux_b, frames, isSolo());
    data.setBpm(context.getBpm());
    data.zero();
    for (auto & [ pos, s ] : chunks) data.assignNamed(s, pos);

    setTrackInfo(TrackInfo(isActive(), data.isClipping(), data.calculateMainRMS()));
    return data;
  }

  // Starts playing `clip`'s own audio (its SampleContent, bounded by its
  // in/out trim points) from the beginning, as a single, plain one-shot
  // SampleClipVoice (SampleTrack.cpp) added directly via addVoice() -
  // column 0 always, a SampleTrack only ever has one clip playing at a
  // time. A no-op if `clip` carries no sample content, or its buffer is
  // empty.
  //
  // Never called directly by anything scheduling playback - only from
  // this class's own render() above, consuming a RenderContext::
  // SampleTrackEvent::START due exactly at the current chunk, and from
  // Player.cpp's live PLAY_SAMPLE_CLIP handler (Session-view triggering
  // has no block to chunk against in the first place, so nothing to gain
  // by routing it through RenderContext too). The voice itself has no
  // idea why it's starting *now* rather than some other frame - that's
  // entirely the caller's problem to get right, this just plays.
  //
  // A *looping* clip has no looping concept at the voice level at all -
  // deliberately: the clip's own length is meant to be the loop, not the
  // audio's own real duration (essentially never an exact multiple of
  // it), so each lap is realized by the *caller* invoking this again,
  // fresh, when the row grid says it's time - SongState.h's own per-row
  // scheduling (via RenderContext) for transport-driven playback,
  // LaunchpadManager::fireOrTriggerClipStep() (already worked this way)
  // for Session-view triggering. stopVoices(0) inside this call already
  // fades out whatever's still sounding from the previous lap if it ran
  // long, and a shorter one simply finishes and stays silent on its own
  // until the next trigger arrives - both halves of "the clip's own
  // length is the loop" fall out of that naturally, no separate lap-
  // timing logic needed here. The same shape a looping *Pattern*'s own
  // note already has (a fresh note-on each time its row wraps around,
  // not one voice sustaining and looping internally) - this just extends
  // it to raw sample playback. Ending a take early for any other reason
  // (a one-shot clip's own real audio outlasting the scene it's placed
  // in, an explicit stop instance, eventually pause/seek) is this same
  // render() loop's own SampleTrackEvent::STOP handling, not this
  // method's concern at all.
  //
  // `song_tempo` is what triggerClip()'s own implementation (SampleTrack.cpp)
  // compares against the clip's own SampleContent::getOriginalTempo() to
  // decide whether to time-stretch - threaded in by the caller rather than
  // read from anywhere on this class, since neither this class nor Clip
  // has any notion of "the song" to read it from itself.
  //
  // `start_offset_frames` (default 0 - "start from this clip's own
  // beginning", every ordinary trigger) shifts that starting point later
  // into the clip's own (post-trim, post-resample/stretch) audio instead -
  // SongState.h's own scheduling is the only caller that ever passes a
  // real value, when the playhead itself lands mid-instance with nothing
  // already sounding to explain why (its own comment has the full
  // reasoning). Clamped against the resolved range, never trusted
  // outright - a clip's own row length never exactly matches its real
  // audio duration (rounded up when it was first derived), so a stale
  // value must fall back to playing nothing rather than reading out of
  // bounds.
  void triggerClip(const Clip & clip, int song_tempo, int start_offset_frames = 0);
};

#endif
