#include "Reverb.h"

#include "MVerb.h"
#include "EffectState.h"

using namespace std;

class ReverbState : public EffectState {
public:
  ReverbState(const ChannelConfiguration & channel_config, float damping_freq, float density, float bandwidth_freq, float decay, float predelay, float size, float gain, float mix, float earlymix)
    : EffectState(channel_config) {
    mverb_.setParameter(MVerb<float>::DAMPINGFREQ, damping_freq);
    mverb_.setParameter(MVerb<float>::DENSITY, density);
    mverb_.setParameter(MVerb<float>::BANDWIDTHFREQ, bandwidth_freq);
    mverb_.setParameter(MVerb<float>::DECAY, decay);
    mverb_.setParameter(MVerb<float>::PREDELAY, predelay);
    mverb_.setParameter(MVerb<float>::SIZE, size);
    mverb_.setParameter(MVerb<float>::GAIN, gain);
    mverb_.setParameter(MVerb<float>::MIX, mix);
    mverb_.setParameter(MVerb<float>::EARLYMIX, earlymix);
    mverb_.setSampleRate(channel_config.getAudioOutSampleRate());
  }

protected:
  void applyEffect(SampleData & input) override {
    auto left_out_ptr = unique_ptr<float[]>(new float[input.size()]);
    auto right_out_ptr = unique_ptr<float[]>(new float[input.size()]);
    auto left_out = left_out_ptr.get(), right_out = right_out_ptr.get();
    
    memset(left_out, 0, input.size() * sizeof(float));
    memset(right_out, 0, input.size() * sizeof(float));

    float * out[2] = { left_out, right_out };
    
    if (input.numberOfChannels() == 2) {
      float * in[2] = { input.getChannelData(0), input.getChannelData(0) };
      
      mverb_.process(in, out, input.size());
      
      auto left_buffer = input.getChannelData(0), right_buffer = input.getChannelData(1);
      for (int i = 0; i < input.size(); i++) {
	left_buffer[i] = left_out[i];
	right_buffer[i] = right_out[i];
      }
    } else {
      float * in[2] = { input.getChannelData(0), input.getChannelData(1) };

      mverb_.process(in, out, input.size());

      auto left_buffer = input.getChannelData(0);
      for (int i = 0; i < input.size(); i++) {
	left_buffer[i] = left_out[i];
      }
    }

    setTrackInfo(TrackInfo( true, input.isClipping()));
  }

private:
  MVerb<float> mverb_;
};

std::unique_ptr<TrackState>
Reverb::createState(const ChannelConfiguration & channel_config) const {
  float damping_freq, density, bandwidth_freq, decay, predelay, size, gain, mix, earlymix;
    
  switch (preset_) {
  case ReverbPreset::NONE:
    damping_freq = damping_freq_;
    density = density_;
    bandwidth_freq = bandwidth_freq_;
    decay = decay_;
    predelay = predelay_;
    size = size_;
    gain = gain_;
    mix = mix_;
    earlymix = earlymix_;
    break;
  case ReverbPreset::SUBTLE:
    damping_freq = 0.0f;
    density = 0.5f;
    bandwidth_freq = 1.0f;
    decay = 0.5f;
    predelay = 0.0f;
    size = 0.5f;
    gain = 1.0f;
    mix = 0.15f;
    earlymix = 0.75f;
    break;
  case ReverbPreset::STADIUM:
    damping_freq = 0.0f;
    density = 0.5f;
    bandwidth_freq = 1.0f;
    decay = 0.5f;
    predelay = 0.0f;
    size = 1.0f;
    mix = 0.35f;
    earlymix = 0.75f;
    break;
  case ReverbPreset::CUPBOARD:
    damping_freq = 0.0f;
    density = 0.5f;
    bandwidth_freq = 1.0f;
    decay = 0.5f;
    predelay = 0.0f;
    size = 0.25f;
    gain = 1.0f;
    mix = 0.35f;
    earlymix = 0.75f;
    break;
  case ReverbPreset::DARK:
    damping_freq = 0.9f;
    density = 0.5f;
    bandwidth_freq = 0.1f;
    decay = 0.5f;
    predelay = 0.0f;
    size = 0.5f;
    gain = 1.0f;
    mix = 0.5f;
    earlymix = 0.75f;
    break;
  case ReverbPreset::HALVES:
    damping_freq = 0.5f;
    density = 0.5f;
    bandwidth_freq = 0.5f;
    decay = 0.5f;
    predelay = 0.5f;
    gain = 1.0f;
    mix = 0.5f;
    earlymix = 0.5f;
    size = 0.75f;
    break;
  }
  
  return make_unique<ReverbState>(channel_config, damping_freq, density, bandwidth_freq, decay, predelay, size, gain, mix, earlymix);
}

void
Reverb::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);
  
  auto preset_text = input.getText("preset");
  if (preset_text == "subtle") preset_ = ReverbPreset::SUBTLE;
  else if (preset_text == "stadium") preset_ = ReverbPreset::STADIUM;
  else if (preset_text == "cupboard") preset_ = ReverbPreset::CUPBOARD;
  else if (preset_text == "dark") preset_ = ReverbPreset::DARK;
  else if (preset_text == "halves") preset_ = ReverbPreset::HALVES;
}

void
Reverb::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("preset", to_string(preset_));
}
