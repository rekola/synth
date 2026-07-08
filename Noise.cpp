#include "Noise.h"

using namespace std;

#include "InstrumentVoice.h"
#include "WaveformType.h"

class NoiseVoice : public InstrumentVoice {
public:
  NoiseVoice(ChannelConfiguration config, float azimuth, float level)
    : InstrumentVoice(config, azimuth, 1.0f, 0.0f), level_(level) {
  }

  SampleData render(int frames) override {    
    float gain = decibelsToGain(getGainDB()) * level_;

    SampleData data(getChannelConfiguration(), frames);
    data.setNonZero();
    
    auto num_channels = data.numberOfChannels();
    auto left_buffer = data.getChannelData(0);
    auto right_buffer = data.getChannelData(1);
    
    if (num_channels == 2) {
      float pan = sin(getAzimuth() / 180.0f * M_PI) / 2;
      if (pan < -0.5) pan = -0.5;
      else if (pan > 0.5) pan = 0.5;
      float left_gain = sqrtf(0.5f - pan) * gain, right_gain = sin(0.5f + pan) * gain;
      
      for (int k = 0; k < frames; k++) {
	left_buffer[k] = left_gain * create_noise();
	right_buffer[k] = right_gain * create_noise();
      }
    } else {
      for (int k = 0; k < frames; k++) {
	left_buffer[k] = create_noise() * gain;       
      }
    }
    
    return data;
  }
  
private:
  static inline float create_noise() {
    return getRandF() * 2.0f - 1.0f;
  }

  float level_;
};

std::unique_ptr<TrackState>
Noise::playNote(const ChannelConfiguration & config, float azimuth, float frequency, float detune, float velocity, float start_phase) const {  
  auto voice = std::make_unique<NoiseVoice>(config, azimuth, level_);
  voice->playNote(frequency, velocity);
  return voice;
}
