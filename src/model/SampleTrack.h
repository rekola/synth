#ifndef _SAMPLETRACK_H_
#define _SAMPLETRACK_H_

#include "InstrumentTrack.h"

// An InstrumentTrack, not a plain Track - a recorded sample is
// positioned/muted/soloed/sent the same way any other instrument is (see
// InstrumentTrack.h's azimuth/elevation/distance/extent/sends/solo/muted),
// and PatternEditor's own color-eligibility/Mute-Solo rendering rule is
// exactly "is this an InstrumentTrack" (see TrackColor.h/renderHeading()),
// so a sample track needs to actually be one to qualify, not just look
// like one via a duplicated TrackType check. instrument_id_ (inherited,
// defaults to 0) has no meaning here yet - sample playback doesn't
// resolve through the instrument pool the way a synthesized voice does -
// but the other InstrumentTrack fields are real and used.
class SampleTrack : public InstrumentTrack {
public:
  SampleTrack(const std::shared_ptr<AudioBuffer> & _sample) : InstrumentTrack(TrackType::SAMPLE), sample(_sample) { }

  const char * getElementName() const override { return "sampleTrack"; }

  void setSample(std::shared_ptr<AudioBuffer> _sample) { sample = _sample; }

private:
  std::shared_ptr<AudioBuffer> sample;

};


#endif
