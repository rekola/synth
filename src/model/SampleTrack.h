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
// Shared by real playback (SampleTrackState::triggerVoice()) and
// background-merge baking (ArrangementOps.cpp's own
// mergeClipToBackground()) so both apply exactly the same resolution
// rather than maintaining two independent copies of the same trim/
// resample/stretch math.
ResolvedSampleAudio resolveSampleAudio(const SampleContent & content, int output_rate, int song_tempo);

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
