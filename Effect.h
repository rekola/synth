#ifndef _EFFECT_H_
#define _EFFECT_H_

#include "Track.h"

class Effect : public Track {
 public:
  Effect() : Track(EFFECT) { }

  std::string getElementName() const override { return "effect"; }

  SampleData render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events) override;
};

#endif
