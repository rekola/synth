#ifndef _SAMPLETRACKSTATE_H_
#define _SAMPLETRACKSTATE_H_

#include "LeafTrackState.h"
#include "RenderContext.h"

class Clip;
class SampleContent;

// The LeafTrackState (see its own doc comment) for raw sample playback -
// plays a clip's own audio (triggerClip() below) rather than resolving
// pattern-driven notes through an InstrumentPool the way
// InstrumentTrackState does. Inherits stopVoices()/stopAllVoices()/mute/
// solo/sends/position/voices_ bookkeeping from LeafTrackState instead of
// reimplementing them, but triggerClip() builds and adds its own voice
// directly (SampleTrack.cpp) rather than going through noteOn() - a raw
// sample clip needs no Instrument/Track indirection resolved for it, and
// none of InstrumentTrackState's note-column/chord/pressure machinery
// applies to it either (no pitch/identity of its own). A SampleTrack has
// exactly two independent, fixed voices, never more - the one clip that
// can be playing (kClipVoiceId), and the current section's own always-on
// background bed (kBackgroundVoiceId), which mixes with it rather than
// being masked by it (real audio genuinely sums; see SongState.h's own
// comment on why this differs from a note track's own background
// Pattern). Both routed through LeafTrackState's own voices_/addVoice()/
// stopVoices() machinery (that base class's own per-voice map key is
// called a "column" - shared infra InstrumentTrackState also uses it for
// real note columns/chords - but nothing here is one: these two fixed
// ids are never a caller-indexable "which slot" parameter anywhere
// outside this class, since a SampleTrack can never have more than these
// two).
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

  // Same-thread setter Player calls every block it actually captures for
  // this track (armed-and-waiting or genuinely recording), so render()'s
  // own TrackInfo can show real input level even while no SampleClipVoice
  // is playing back to produce one of its own.
  void setInputLoudness(float rms) { input_loudness_ = rms; }

  // Splits the block at every RenderContext::getPendingSampleEvents()
  // entry due within it (a clip or background-bed start/stop, this
  // class's own sibling timeline to InstrumentTrackState::render()'s
  // pending_events_ - see SampleTrackEvent's own comment), applies each
  // one exactly there, then keeps rendering - the same chunked shape
  // InstrumentTrackState::render() uses for pattern note-on/off events,
  // just against this track's own timeline instead. More than one event
  // can be due on the same frame now (the clip voice and the background
  // voice are independent), so every event at a given frame is applied
  // before rendering resumes, not just the first one found there. See
  // SampleTrackEvent's own comment for why a sound producer should never
  // need to do this itself.
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
	  for (auto & event : it->second) {
	    auto voice_id = event.is_background ? kBackgroundVoiceId : kClipVoiceId;
	    if (event.kind == SampleTrackEvent::START && event.content) {
	      // context.getBpm() is exactly the song's own current tempo
	      // (SongState::initialize()'s render_context_.setBpm(tempo_)) -
	      // triggerVoice()'s own tempo-stretch decision needs it in
	      // the same units SampleContent::getOriginalTempo() is
	      // authored in.
	      triggerVoice(*event.content, static_cast<int>(context.getBpm()), event.start_offset_frames, voice_id);
	    } else if (event.kind == SampleTrackEvent::STOP) {
	      // A short natural release, not a hard cut - stopping outright
	      // here would click (SampleTrackEvent's own comment).
	      stopVoices(voice_id);
	    }
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

    setTrackInfo(TrackInfo(isActive(), data.isClipping(), isActive() ? data.calculateMainRMS() : input_loudness_,
      getSends().main, getSends().a, getSends().b, getPosition().azimuth, true));
    return data;
  }

  // A real Clip's own entry point (Player.cpp's own Session-view live
  // triggering, and SongState.h's own transport-driven clip scheduling,
  // via triggerVoice() below) - a no-op if `clip` carries no sample
  // content at all. Always the clip voice (kClipVoiceId) - Player.cpp has
  // no notion of the background bed at all, that's SongState.h's own
  // concern (addPendingSampleStart()'s own `is_background` flag).
  //
  // A *looping* clip has no looping concept at the voice level at all -
  // deliberately: the clip's own length is meant to be the loop, not the
  // audio's own real duration (essentially never an exact multiple of
  // it), so each lap is realized by the *caller* invoking this again,
  // fresh, when the row grid says it's time - SongState.h's own per-row
  // scheduling (via RenderContext) for transport-driven playback,
  // LaunchpadManager::fireOrTriggerClipStep() (already worked this way)
  // for Session-view triggering. triggerVoice()'s own stopVoices(voice_id)
  // already fades out whatever's still sounding from the previous lap if
  // it ran long, and a shorter one simply finishes and stays silent on
  // its own until the next trigger arrives - both halves of "the clip's
  // own length is the loop" fall out of that naturally, no separate
  // lap-timing logic needed here. The same shape a looping *Pattern*'s
  // own note already has (a fresh note-on each time its row wraps
  // around, not one voice sustaining and looping internally) - this just
  // extends it to raw sample playback. Ending a take early for any other
  // reason (a one-shot clip's own real audio outlasting the section it's
  // placed in, an explicit stop instance, eventually pause/seek) is this
  // same render() loop's own SampleTrackEvent::STOP handling, not this
  // method's concern at all.
  //
  // `song_tempo` is what triggerVoice()'s own implementation
  // (SampleTrack.cpp, via resolveRealtimeSampleAudio()) compares against
  // the content's own SampleContent::getOriginalTempo() to decide whether
  // to time-stretch - threaded in by the caller rather than read from
  // anywhere on this class, since neither this class nor Clip has any
  // notion of "the song" to read it from itself.
  //
  // `start_offset_frames` (default 0 - "start from this content's own
  // beginning", every ordinary trigger) shifts that starting point later
  // into the content's own post-trim audio instead - SongState.h's own
  // scheduling is the only caller that ever
  // passes a real value, when the playhead itself lands mid-instance with
  // nothing already sounding to explain why (its own comment has the full
  // reasoning). Clamped against the resolved range, never trusted
  // outright - a clip's own row length never exactly matches its real
  // audio duration (rounded up when it was first derived), so a stale
  // value must fall back to playing nothing rather than reading out of
  // bounds.
  void triggerClip(const Clip & clip, int song_tempo, int start_offset_frames = 0);

private:
  // The two, and only two, voices a SampleTrack ever has - see this
  // class's own doc comment for why these stay fixed, named ids rather
  // than a general "which voice" parameter anywhere outside this class.
  static constexpr int kClipVoiceId = 0;
  static constexpr int kBackgroundVoiceId = 1;

  // The shared implementation behind both the clip voice (triggerClip()
  // above) and the background-bed voice (this class's own render(),
  // consuming a SampleTrackEvent::is_background START) - builds and adds
  // a SampleClipVoice (SampleTrack.cpp) as the given fixed voice via
  // addVoice(), stopping whatever was already that same voice first
  // (stopVoices(voice_id)) so a fresh trigger on one never disturbs the
  // other. A no-op if `content` carries no buffer.
  void triggerVoice(const SampleContent & content, int song_tempo, int start_offset_frames, int voice_id);

  float input_loudness_ = 0.0f;
};

#endif
