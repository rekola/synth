#ifndef _VOICEPOOL_H_
#define _VOICEPOOL_H_

#include "State.h"
#include "TrackState.h"
#include "SampleData.h"

#include <vector>
#include <memory>
#include <algorithm>

static inline bool is_not_playing(const std::pair<int, std::unique_ptr<TrackState> > & a) { return !a.second->isPlaying(); }

class VoicePool : public State {
 public:
  VoicePool(unsigned int _outSampleRate) : State(_outSampleRate) { }
  
  void render(SampleData & output, size_t frames, size_t offset) {
    for (auto & [ id, voice ] : voices) {
      if (voice->isPlaying()) {
	auto voice_data = voice->render(frames);
	output.mix(voice_data, offset);
      }
    }   
  }

  // std::vector<std::unique_ptr<TrackState> > & getVoices() { return voices; }

  void clear() { voices.clear(); }
  
  size_t getVoiceCount() const {
    size_t n = 0;
    for (auto & [ id, voice ] : voices) {
      if (voice->isPlaying()) n++;
    }
    return n;
  }
  
  size_t getAllocatedVoiceCount() const { return voices.size(); }

  TrackState & addVoice(int identifier, std::unique_ptr<TrackState> voice) {
    voices.erase(std::remove_if(voices.begin(), voices.end(), is_not_playing), voices.end());
 	 
    voices.push_back(std::pair(identifier, std::move(voice)));
    return *(voices.back().second);
  }

  void stopVoices(size_t column) {
    for (auto & [id, voice] : voices) {
      if (column == id && voice->isPlaying()) {
	voice->stopNote();
      }
    }
  }

  void applyAftertouch(size_t column, float aftertouch) {
    for (auto & [id, voice] : voices) {
      if (column == id && voice->isPlaying()) {
	voice->applyAftertouch(aftertouch);
      }
    }    
  }
  
 private:
  std::vector<std::pair<int, std::unique_ptr<TrackState> > > voices;
};

#endif
