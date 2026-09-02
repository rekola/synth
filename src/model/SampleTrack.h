#ifndef _SAMPLETRACK_H_
#define _SAMPLETRACK_H_

#include "LeafTrack.h"

class SongStructure;

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
// recording/loaded file (Clip::getSample()/setSample()) - the same
// clip-list mechanism InstrumentTrack already uses for its own reusable
// Pattern content, just with raw audio instead of notes. "Multiple audio
// files" per track is multiple entries in that list, launched via
// Session view/ArrangementGrid like any other track's clips - never
// addressed by a pattern-row Note value.
class SampleTrack : public LeafTrack {
public:
  SampleTrack() : LeafTrack(TrackType::SAMPLE) { }

  const char * getElementName() const override { return "sampleTrack"; }
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const override;
};


#endif
