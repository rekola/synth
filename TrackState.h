#ifndef _TRACKSTATE_H_
#define _TRACKSTATE_H_

#include "State.h"
#include "VoicePool.h"

class TrackState : public State {
 public:
  TrackState(unsigned int _outSampleRate) : State(_outSampleRate), voices(_outSampleRate) { }

  const VoicePool & getVoices() const { return voices; }
  VoicePool & getVoices() { return voices; }
  
 private:
  VoicePool voices;
};

#endif
