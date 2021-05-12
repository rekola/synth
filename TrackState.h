#ifndef _TRACKSTATE_H_
#define _TRACKSTATE_H_

#include "State.h"
#include "EffectState.h"
#include "InstrumentVoice.h"
#include "VoicePool.h"

#include <memory>
#include <vector>

class TrackState : public State {
 public:
  TrackState(unsigned int _outSampleRate) : State(_outSampleRate), voices(_outSampleRate) { }

  bool isInitialized() const { return is_initialized; }
  void initialize(const std::vector<std::unique_ptr<Effect> > & effects) {
    for (auto & effect : effects) {
      effect_states.push_back(effect->createState(getOutSampleRate()));
    }    
    is_initialized = true;
  }

  void applyEffects(SampleData & data) {
    for (auto & state : effect_states) {
      state->apply(data);
    }
  }

  const VoicePool & getVoices() const { return voices; }
  VoicePool & getVoices() { return voices; }
  
 private:
  std::vector<std::unique_ptr<EffectState> > effect_states; 
  VoicePool voices;
  bool is_initialized = false;
};

#endif
