#ifndef _VOICEPOOL_H_
#define _VOICEPOOL_H_

#include "State.h"
#include "InstrumentVoice.h"
#include "SampleData.h"

#include <vector>
#include <memory>

class VoicePool : public State {
 public:
  VoicePool(unsigned int _outSampleRate) : State(_outSampleRate) { }
  
  void render(SampleData & output, size_t frames, size_t offset) {
    for (auto & voice : voices) {
      if (voice->isPlaying()) {
	auto voice_data = voice->render(frames);
	output.mix(voice_data, offset);	
      }
    }   
  }

  std::vector<std::unique_ptr<InstrumentVoice> > & getVoices() { return voices; }

  void clear() { voices.clear(); }
  
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

  InstrumentVoice & addVoice(std::unique_ptr<InstrumentVoice> voice) {
    voices.push_back(std::move(voice));
    return *(voices.back());
  }
  
 private:
  std::vector<std::unique_ptr<InstrumentVoice> > voices;
};

#endif
