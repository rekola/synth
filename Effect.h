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

  SampleData render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Instrument> > & instruments, TrackEventQueue & events) override;  
};

#endif
