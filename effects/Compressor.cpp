#include "Compressor.h"

#include "../TrackState.h"

#include <cmath>
#include <cstddef>

using namespace std;

class CompressorState : public TrackState {
public:
  CompressorState(const ChannelConfiguration & _channel_config, const Compressor & compressor)
    : TrackState(_channel_config),
      f_thresh(compressor.getTreshold()),
      f_ratio(compressor.getRatio()),
      f_attack(compressor.getAttack()),
      f_release(compressor.getRelease())
  { }
  
  SampleData render(int frames) override {
    auto input = TrackState::render(frames);
    
    auto in = input.getChannelData(0);
    auto n = input.size();	
	
    float current_rms = rms(in, n);

    // calculate a target amp if we're over the threshold. use the current attack time provided by the user as the ramp duration

/*
if attackFlag == 0 && RMS > thresh,
	attackFlag =1;
	ramp gain down;
else if attackFlag ==1 && RMS > thresh,
	adjust gain;
else if RMS < thresh,
	attackFlag = 0;
	ramp gain back to unity
*/

    if (current_rms > f_thresh && attackFlag == 0) {
      float overage, targetAmp;
      
      attackFlag = 1;
      
      overage = current_rms - f_thresh;
      overage /= f_ratio;
      targetAmp = f_thresh + overage;
      
      // rampcmd function takes amplitude in RMS, time in ms
      ramp(targetAmp / current_rms, f_attack);
    } else if (current_rms > f_thresh && attackFlag == 1) {
      float overage, targetAmp;
      
      overage = current_rms - f_thresh;
      overage /= f_ratio;
      targetAmp = f_thresh + overage;
      
      // rampcmd function takes amplitude in RMS, time in ms
      ramp(targetAmp/current_rms, f_attack);
    } else if (current_rms < f_thresh) {
      // rampcmd function takes amplitude in RMS, time in ms
      ramp(1.0, f_release);
      attackFlag = 0;
    }
        
    while (n--) {
      if (i_fadesamps-- > 1) {
	f_coeff = f_coeff + f_gain_change;
      } else {
	i_fadesamps = 0;
	f_coeff = f_gain_target;
      }
      
      // makeup gain stage here	
      (*in++) *= f_coeff;
    }

    return input;
  }

  TrackInfo getInfo() const override {
    return TrackInfo(attackFlag != 0);
  }

protected:
  float rms(float * block, size_t n) {
    float sum = 0;
    for (size_t i = 0; i < n; i++) {
      sum += block[i] * block[i];
    }
    return sqrt(sum / n);
  }

  // Ramp function to be used in attack and release
  void ramp(float target, float time) {
    f_gain_target = target < 0 ? 0.0f : target;
    f_gain_target = f_gain_target > 1.0f ? 1.0f : f_gain_target;
    
    float ampdif = f_gain_target - f_coeff;
    
    i_fadesamps = getChannelConfiguration().getAudioOutSampleRate() * (time / 1000);
    f_gain_change = ampdif / (i_fadesamps - 1);
  } 
  
private:
  float f_thresh;
  float f_ratio;
  float f_attack;
  float f_release;

  // Output Gain/Makeup Gain: the output level after compression
  // Knee: a degree of smoothing in the output graph between the uncompressed and compressed ranges

  float f_coeff = 1.0f;
  float f_gain_change = 0; // gain increment
  float f_gain_target = 1.0f;
  int i_fadesamps = 0;  // Fadetime in samples	

  int attackFlag = 0;
};

std::unique_ptr<TrackState>
Compressor::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<CompressorState>(channel_config, *this);
}

void
Compressor::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);
}

void
Compressor::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);
}
