#ifndef _SAMPLETRACK_H_
#define _SAMPLETRACK_H_

#include "Track.h"

class SampleTrack : public Track {
public:
  SampleTrack(const std::shared_ptr<AudioBuffer> & _sample) : Track(TrackType::SAMPLE), sample(_sample) { }

  const char * getElementName() const override { return "sampleTrack"; }

  void setSample(std::shared_ptr<AudioBuffer> _sample) { sample = _sample; }

private:
  std::shared_ptr<AudioBuffer> sample;

};


#endif
