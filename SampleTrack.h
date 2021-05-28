#ifndef _SAMPLETRACK_H_
#define _SAMPLETRACK_H_

#include "Track.h"

class SampleTrack : public Track {
public:
  SampleTrack(const std::shared_ptr<SampleData> & _sample) : Track(SAMPLE), sample(_sample) { }

  SampleData render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Instrument> > & instruments, TrackEventQueue & events) override {
    return SampleData(1, frames);
  }

  void setSample(std::shared_ptr<SampleData> _sample) { sample = _sample; }

private:
  std::shared_ptr<SampleData> sample;

};


#endif
