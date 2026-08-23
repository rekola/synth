#ifndef _SAMPLETRACK_H_
#define _SAMPLETRACK_H_

#include "LeafTrack.h"

// A LeafTrack, not a plain Track - a recorded sample is positioned/muted/
// soloed/sent the same way any other leaf track is (see LeafTrack.h's
// azimuth/elevation/distance/extent/sends/solo/muted), and PatternEditor's
// own color-eligibility/Mute-Solo rendering rule is exactly "is this a
// LeafTrack" (SongStructure.cpp/PatternEditor::renderHeading()), so a
// sample track needs to actually be one to qualify, not just look like one
// via a duplicated TrackType check. No instrument_id_ here - sample
// playback doesn't resolve through the instrument pool the way a
// synthesized voice does.
class SampleTrack : public LeafTrack {
public:
  SampleTrack(const std::shared_ptr<AudioBuffer> & _sample) : LeafTrack(TrackType::SAMPLE), sample(_sample) { }

  const char * getElementName() const override { return "sampleTrack"; }

  void setSample(std::shared_ptr<AudioBuffer> _sample) { sample = _sample; }

private:
  std::shared_ptr<AudioBuffer> sample;

};


#endif
