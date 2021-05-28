#ifndef _EFFECT_H_
#define _EFFECT_H_

#include "Track.h"

#include <memory>

class EffectState;

class Effect : public Track {
 public:
  Effect() : Track(EFFECT) { }

  virtual std::unique_ptr<EffectState> createState(unsigned int outSamplerate) const = 0;
  std::string getElementName() const override { return "effect"; }

  SampleData render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Instrument> > & instruments, TrackEventQueue & events) override;  
};

#endif
