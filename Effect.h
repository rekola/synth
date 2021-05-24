#ifndef _EFFECT_H_
#define _EFFECT_H_

#include "Track.h"
#include "EffectState.h"
#include "SampleData.h"

#include <memory>

class Effect : public Track {
 public:
  Effect() : Track(EFFECT) { }

  virtual std::unique_ptr<EffectState> createState(unsigned int outSamplerate) const = 0;

  SampleData render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Instrument> > & instruments, TrackEventQueue & events) override {
    if (getChildren().empty()) {
      return SampleData(1, frames);
    } else {
      auto it = getChildren().begin();
      auto sd = (*it)->render(frames, song_state, instruments, events);
      for (it++; it != getChildren().end(); it++) {
	auto sd2 = (*it)->render(frames, song_state, instruments, events);
	sd.mix(sd2);
      }
      return sd;
    }
  }
};

#endif
