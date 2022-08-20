#ifndef _PERCUSSIONTRACK_H_
#define _PERCUSSIONTRACK_H_

#include "InstrumentTrack.h"

class PercussionTrack : public InstrumentTrack {
 public:
  PercussionTrack() : InstrumentTrack(TrackType::PERCUSSION_CONTROL) { }
  
 private:
};

#endif
