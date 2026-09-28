#include "Phaser.h"

#include "EffectTrackState.h"
#include "EffectVoiceState.h"

#include "../dsp/AllpassFilter.h"

#include <array>
#include <cmath>
#include <vector>

using namespace std;

namespace {

// Actual DSP, shared by PhaserTrackState and PhaserVoiceState - see
// EffectTrackState.h/EffectVoiceState.h and
// plans/trackstate-voicestate-split.md.
class PhaserDsp {
public:
  PhaserDsp(const ChannelConfiguration & channel_config, int stages, float rate, float min_freq, float max_freq, float feedback, float mix)
    : stages_(stages < 1 ? 1 : stages), rate_(rate), min_freq_(min_freq), max_freq_(max_freq),
      feedback_(feedback), mix_(mix), sample_rate_(static_cast<float>(channel_config.getAudioOutSampleRate()))
  {
    main_chains_.resize(static_cast<size_t>(channel_config.numberOfChannels()));
    for (auto & chain : main_chains_) chain.resize(static_cast<size_t>(stages_));
    main_feedback_.resize(main_chains_.size(), 0.0f);
    for (auto & chain : aux_chains_) chain.resize(static_cast<size_t>(stages_));
  }

  // Filters every channel - Main and AuxA/AuxB alike, same reasoning as
  // BiquadFilter/Amplifier/EnvelopeFilter/Compressor/Tremolo: the
  // reverb/delay bus should hear the same sweep the dry signal does.
  // Returns whether there was anything to process this block, for the
  // caller's own isEffectActive() bookkeeping.
  bool applyEffect(AudioBuffer & input) {
    if (input.numberOfChannels() == 0) return false;

    int numSamples = input.size();
    int mainChannels = input.regularChannelCount();
    if (mainChannels > 0) main_ever_present_ = true;

    double step = 2.0 * M_PI * static_cast<double>(rate_) / static_cast<double>(sample_rate_);
    // Exponential (not linear) sweep between min_freq_/max_freq_ - a
    // musically even-sounding glide across the range, the same reason a
    // pitch/frequency control is conventionally logarithmic rather than
    // linear anywhere else in audio.
    float log_ratio = std::log(max_freq_ / min_freq_);

    for (int i = 0; i < numSamples; i++) {
      double lfo_unit = 0.5 + 0.5 * std::sin(phi_);
      float fc = min_freq_ * std::exp(static_cast<float>(lfo_unit) * log_ratio);
      float a = AllpassStage<float>::coefficientFor(fc, sample_rate_);

      for (size_t c = 0; c < main_chains_.size(); c++) {
        if (static_cast<int>(c) < mainChannels) {
          auto buffer = input.getChannelData(static_cast<int>(c));
          float dry = buffer[i];
          float wet = dry + feedback_ * main_feedback_[c];
          for (auto & stage : main_chains_[c]) wet = stage.process(wet, a);
          main_feedback_[c] = wet;
          buffer[i] = dry * (1.0f - mix_) + wet * mix_;
        } else if (main_ever_present_) {
          float wet = feedback_ * main_feedback_[c];
          for (auto & stage : main_chains_[c]) wet = stage.process(wet, a);
          main_feedback_[c] = wet;
        }
      }

      for (int ax = 0; ax < 2; ax++) {
        auto * buf = input.getChannel(ax == 0 ? Channel::AuxA : Channel::AuxB);
        if (buf) {
          aux_ever_present_[static_cast<size_t>(ax)] = true;
          float dry = buf[i];
          float wet = dry + feedback_ * aux_feedback_[static_cast<size_t>(ax)];
          for (auto & stage : aux_chains_[static_cast<size_t>(ax)]) wet = stage.process(wet, a);
          aux_feedback_[static_cast<size_t>(ax)] = wet;
          buf[i] = dry * (1.0f - mix_) + wet * mix_;
        } else if (aux_ever_present_[static_cast<size_t>(ax)]) {
          float wet = feedback_ * aux_feedback_[static_cast<size_t>(ax)];
          for (auto & stage : aux_chains_[static_cast<size_t>(ax)]) wet = stage.process(wet, a);
          aux_feedback_[static_cast<size_t>(ax)] = wet;
        }
      }

      phi_ += step;
    }
    return true;
  }

private:
  int stages_;
  float rate_, min_freq_, max_freq_, feedback_, mix_;
  float sample_rate_;
  double phi_ = 0.0;

  std::vector<std::vector<AllpassStage<float>>> main_chains_;
  std::vector<float> main_feedback_;
  bool main_ever_present_ = false;

  std::array<std::vector<AllpassStage<float>>, 2> aux_chains_;
  std::array<float, 2> aux_feedback_ { 0.0f, 0.0f };
  std::array<bool, 2> aux_ever_present_ { false, false };
};

class PhaserTrackState : public EffectTrackState {
public:
  PhaserTrackState(const ChannelConfiguration & channel_config, int stages, float rate, float min_freq, float max_freq, float feedback, float mix)
    : EffectTrackState(channel_config), dsp_(channel_config, stages, rate, min_freq, max_freq, feedback, mix) { }

protected:
  void applyEffect(AudioBuffer & input) override {
    setEffectActive(dsp_.applyEffect(input));
    setTrackInfo(TrackInfo( isEffectActive(), input.isClipping() ));
  }

private:
  PhaserDsp dsp_;
};

class PhaserVoiceState : public EffectVoiceState {
public:
  PhaserVoiceState(const ChannelConfiguration & channel_config, int stages, float rate, float min_freq, float max_freq, float feedback, float mix)
    : EffectVoiceState(channel_config), dsp_(channel_config, stages, rate, min_freq, max_freq, feedback, mix) { }

protected:
  void applyEffect(AudioBuffer & input) override {
    setEffectActive(dsp_.applyEffect(input));
  }

private:
  PhaserDsp dsp_;
};

}

std::unique_ptr<TrackState>
Phaser::createState(const ChannelConfiguration & config, const SongStructure & structure) const {
  return make_unique<PhaserTrackState>(config, stages_, rate_, min_freq_, max_freq_, feedback_, mix_);
}

std::unique_ptr<VoiceState>
Phaser::createVoiceState(const ChannelConfiguration & config) const {
  return make_unique<PhaserVoiceState>(config, stages_, rate_, min_freq_, max_freq_, feedback_, mix_);
}

void
Phaser::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);

  stages_ = input.get<int>("stages", 4);
  rate_ = input.get<float>("rate", 0.5f);
  min_freq_ = input.get<float>("minFreq", 200.0f);
  max_freq_ = input.get<float>("maxFreq", 2000.0f);
  feedback_ = input.get<float>("feedback", 0.0f);
  mix_ = input.get<float>("mix", 0.5f);
}

void
Phaser::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("stages", stages_, 4);
  output.set("rate", rate_, 0.5f);
  output.set("minFreq", min_freq_, 200.0f);
  output.set("maxFreq", max_freq_, 2000.0f);
  output.set("feedback", feedback_, 0.0f);
  output.set("mix", mix_, 0.5f);
}
