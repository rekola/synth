#ifndef _SAMPLETRACK_H_
#define _SAMPLETRACK_H_

#include "Track.h"

class SampleTrack : public Track {
public:
  SampleTrack(const std::shared_ptr<SampleData> & _sample) : Track(TrackType::SAMPLE), sample(_sample) { }

  std::string getElementName() const override { return "sampleTrack"; }

  void setSample(std::shared_ptr<SampleData> _sample) { sample = _sample; }

private:
  std::shared_ptr<SampleData> sample;

};


#endif
