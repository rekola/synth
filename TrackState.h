#ifndef _TRACKSTATE_H_
#define _TRACKSTATE_H_

#include "State.h"
#include "EffectState.h"
#include "InstrumentVoice.h"

#include <memory>
#include <vector>

class TrackState : public State {
 public:
  TrackState(unsigned int _outSampleRate) : State(_outSampleRate) { }

  bool isInitialized() const { return is_initialized; }
  void initialize(const std::vector<std::shared_ptr<Effect> > & effects) {
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

  void clearVoices() { voices.clear(); }
  
  size_t getVoiceCount() const {
    size_t n = 0;
    for (auto & voice : voices) {
      if (voice->isPlaying()) n++;
    }
    return n;
  }
  
  size_t getAllocatedVoiceCount() const { return voices.size(); }

  void stopNote(size_t column) {
    for (auto & voice : voices) {
      if (column == voice->getIdentifier() && voice->isPlaying()) {
	voice->stopNote();
      }
    }
  }

  void playNote(size_t column, float frequency, float velocity, float delay, float detune, const Instrument & instrument) {
    bool voice_found = false;
    for (auto & voice : voices) {
      if (!voice_found && !voice->isPlaying()) {
	voice->setIdentifier(column);
	voice->playNote(frequency, velocity, delay, detune);
	voice_found = true;
      } else if (column == voice->getIdentifier() && voice->isPlaying()) {
	voice->stopNote();
      }
    }
    if (!voice_found) {
      voices.push_back(instrument.createVoice(getOutSampleRate(), column));
      voices.back()->playNote(frequency, velocity, delay, detune);
    }
  }

  void renderVoices(SampleData & output, size_t frames, size_t offset) {
    for (auto & voice : voices) {
      if (voice->isPlaying()) {
	auto voice_data = voice->render(frames);
	output.mix(voice_data, offset);	
      }
    }   
  }

 private:
  std::vector<std::unique_ptr<EffectState> > effect_states; 
  std::vector<std::unique_ptr<InstrumentVoice> > voices;
  bool is_initialized = false;
};

#endif
