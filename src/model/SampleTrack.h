#ifndef _SAMPLETRACK_H_
#define _SAMPLETRACK_H_

#include "LeafTrack.h"

#include <memory>

class SongStructure;
class SampleContent;
class AudioBuffer;

// resolveSampleAudio()'s own result - the buffer a trigger would actually
// play from (post-trim, post-resample-to-output_rate, post-tempo-stretch)
// plus the frame range within it, or a null `samples` when `content` has
// nothing playable (no buffer, or a degenerate trim/stretch result).
struct ResolvedSampleAudio {
  std::shared_ptr<AudioBuffer> samples;
  int in_frame = 0;
  int out_frame = 0;
};

// Resolves what `content` would actually sound like if triggered right
// now: trimmed to its own in/out points, resampled to `output_rate` if its
// own native rate disagrees, and pitch-preserving tempo-stretched to
// `song_tempo` if its own recorded tempo disagrees (SampleContent::
// getOriginalTempo()'s own "0 means unknown, never stretch" convention).
// Always fully materializes its result (a real, synchronous whole-buffer
// resample/stretch pass) - fine for background-merge baking
// (ArrangementOps.cpp's own mergeClipToBackground(), a one-time, off-the-
// audio-thread bake that genuinely needs real PCM to sum into the
// background bed), wrong for a real-time voice trigger (see
// resolveRealtimeSampleAudio() below for why); SampleTrackState::
// triggerVoice() only still calls this when tempo-stretching is also
// needed, since SoundTouch has no real-time path in this codebase yet
// (docs/known_bugs.md's own entry on that).
ResolvedSampleAudio resolveSampleAudio(const SampleContent & content, int output_rate, int song_tempo);

// resolveRealtimeSampleAudio()'s own result - like ResolvedSampleAudio,
// but `samples` may still be at `content`'s own native rate rather than
// pre-resampled to output_rate, and `playback_ratio` (native frames per
// output frame - exactly 1.0 when no real-time resampling is needed) is
// what a SampleClipVoice actually reads it at, advancing its own
// fractional `start_frame` by this amount every output sample instead of
// always 1.0. `start_frame` is a frame position, not a count, so it stays
// floating point even though `end_frame` (a fixed boundary, never
// fractionally approached from outside) doesn't need to.
struct RealtimeSampleAudio {
  std::shared_ptr<AudioBuffer> samples;
  double start_frame = 0.0;
  int end_frame = 0;
  double playback_ratio = 1.0;
};

// The real-time-triggered counterpart to resolveSampleAudio() above, used
// by SampleTrackState::triggerVoice() for the ordinary (no tempo-stretch
// needed) case - never resamples the whole buffer synchronously the way
// resolveSampleAudio() does. `SampleClipVoice` (SampleTrack.cpp) instead
// reads straight from the native-rate buffer, advancing a fractional
// position by `playback_ratio` per output sample and interpolating
// between its two neighboring native samples - a real, potentially
// audible-dropout-causing amount of work to do synchronously inside one
// trigger call on the audio thread, for a long clip. Falls back to
// resolveSampleAudio()'s own fully-materialized result (playback_ratio
// 1.0, already at output_rate) whenever tempo-stretching is also needed -
// that step has no real-time path of its own yet (docs/known_bugs.md),
// so its synchronous cost isn't something this function fixes.
RealtimeSampleAudio resolveRealtimeSampleAudio(const SampleContent & content, int output_rate, int song_tempo);

// A LeafTrack, not a plain Track - a recorded sample is positioned/muted/
// soloed/sent the same way any other leaf track is (see LeafTrack.h's
// azimuth/elevation/distance/extent/sends/solo/muted), and PatternEditor's
// own color-eligibility/Mute-Solo rendering rule is exactly "is this a
// LeafTrack" (SongStructure.cpp/PatternEditor::renderHeading()), so a
// sample track needs to actually be one to qualify, not just look like one
// via a duplicated TrackType check. No instrument_id_ here - sample
// playback doesn't resolve through the instrument pool the way a
// synthesized voice does.
//
// Holds no audio itself - a SampleTrack's actual content is its own
// Song::getClips(track_id) list, each entry a Clip carrying one audio
// recording/loaded file (Clip::getSampleContent()) - the same clip-list
// mechanism InstrumentTrack already uses for its own reusable Pattern
// content, just with raw audio instead of notes. "Multiple audio files"
// per track is multiple entries in that list, launched via Session view/
// ArrangementGrid like any other track's clips - never addressed by a
// pattern-row Note value.
class SampleTrack : public LeafTrack {
public:
  SampleTrack() : LeafTrack(TrackType::SAMPLE) { }

  const char * getElementName() const override { return "sampleTrack"; }
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const override;
};


#endif
