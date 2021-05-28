#ifndef _TRACKSTATE_H_
#define _TRACKSTATE_H_

#include "State.h"
#include "TrackInfo.h"

class SampleData;

class TrackState : public State {
 public:
  TrackState(unsigned int _samplerate) : State(_samplerate) { }

  virtual void apply(SampleData & input_data) { }
  virtual TrackInfo getInfo() const { return TrackInfo(true); }
};

#endif
