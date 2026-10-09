#include "Equalizer.h"

#include "EffectTrackState.h"
#include "EffectVoiceState.h"

#include "../dsp/Biquad.h"
#include "../dsp/ChannelBank.h"
#include "../util/constants.h"

#include <memory>
#include <string>
#include <vector>

using namespace std;

namespace {

struct BandSpec {
  const char * prefix;
  FilterType type;
  float default_freq, default_q;
};

// Attribute names are prefix + Freq/Gain/Q (shelves have no Q attribute).
const BandSpec kSpecs[Equalizer::kBands] = {
  { "low", FilterType::lowshelf, 100.0f, 0.707f },
  { "lowMid", FilterType::peak, 400.0f, 1.0f },
  { "highMid", FilterType::peak, 2500.0f, 1.0f },
  { "high", FilterType::highshelf, 8000.0f, 0.707f },
};

bool isShelf(int band) { return band == 0 || band == Equalizer::kBands - 1; }

// Shared by the track-tree and per-note states. Slot layout as in
// BiquadFilter: Main channels first, then AuxA and AuxB in their own slots.
class EqualizerDsp {
public:
  EqualizerDsp(const ChannelConfiguration & channel_config, const Equalizer::Band (&bands)[Equalizer::kBands])
    : main_slots_(channel_config.numberOfChannels())
  {
    const double rate = channel_config.getAudioOutSampleRate();
    for (int b = 0; b < Equalizer::kBands; b++) {
      if (bands[b].gain_db == 0.0f) continue;
      auto c = Biquad<double>(kSpecs[b].type, bands[b].freq / rate, bands[b].q, bands[b].gain_db).coefficients();
      banks_.emplace_back(main_slots_ + 2, dsp::BiquadBank::Coefficients { c.a0, c.a1, c.a2, c.b1, c.b2 });
    }
  }

  bool applyEffect(AudioBuffer & input) {
    if (input.numberOfChannels() == 0) return false;
    if (banks_.empty()) return true;

    int main_channels = input.regularChannelCount();
    if (main_channels > main_slots_) main_channels = main_slots_;
    float * main_planes[dsp::kMaxBankChannels] = {};
    for (int c = 0; c < main_channels; c++) main_planes[c] = input.getChannelData(c);
    float * aux_planes[2] = { input.getChannel(Channel::AuxA), input.getChannel(Channel::AuxB) };

    auto numSamples = input.size();
    size_t offset = 0;
    while (numSamples) {
      int block = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;
      for (auto & bank : banks_) {
        for (int c = 0; c < main_slots_; c++) bank.plane(c) = main_planes[c] ? main_planes[c] + offset : nullptr;
        for (int a = 0; a < 2; a++) bank.plane(main_slots_ + a) = aux_planes[a] ? aux_planes[a] + offset : nullptr;
        bank.apply(block);
      }
      offset += static_cast<size_t>(block);
      numSamples -= block;
    }
    return true;
  }

private:
  int main_slots_;
  vector<dsp::BiquadBank> banks_;
};

class EqualizerTrackState : public EffectTrackState {
public:
  EqualizerTrackState(const ChannelConfiguration & config, const Equalizer::Band (&bands)[Equalizer::kBands])
    : EffectTrackState(config), dsp_(config, bands) { }

protected:
  void applyEffect(AudioBuffer & input) override {
    setEffectActive(dsp_.applyEffect(input));
    setTrackInfo(TrackInfo( isEffectActive(), input.isClipping() ));
  }

private:
  EqualizerDsp dsp_;
};

class EqualizerVoiceState : public EffectVoiceState {
public:
  EqualizerVoiceState(const ChannelConfiguration & config, const Equalizer::Band (&bands)[Equalizer::kBands])
    : EffectVoiceState(config), dsp_(config, bands) { }

protected:
  void applyEffect(AudioBuffer & input) override { setEffectActive(dsp_.applyEffect(input)); }

private:
  EqualizerDsp dsp_;
};

}

Equalizer::Equalizer() {
  for (int b = 0; b < kBands; b++) bands_[b] = { kSpecs[b].default_freq, 0.0f, kSpecs[b].default_q };
}

unique_ptr<TrackState>
Equalizer::createState(const ChannelConfiguration & config, const SongStructure &) const {
  return make_unique<EqualizerTrackState>(config, bands_);
}

unique_ptr<VoiceState>
Equalizer::createVoiceState(const ChannelConfiguration & config) const {
  return make_unique<EqualizerVoiceState>(config, bands_);
}

void
Equalizer::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);
  for (int b = 0; b < kBands; b++) {
    string p = kSpecs[b].prefix;
    bands_[b].freq = input.get<float>(p + "Freq", kSpecs[b].default_freq);
    bands_[b].gain_db = input.get<float>(p + "Gain", 0.0f);
    bands_[b].q = isShelf(b) ? kSpecs[b].default_q : input.get<float>(p + "Q", kSpecs[b].default_q);
  }
}

void
Equalizer::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);
  for (int b = 0; b < kBands; b++) {
    string p = kSpecs[b].prefix;
    output.set(p + "Freq", bands_[b].freq, kSpecs[b].default_freq);
    output.set(p + "Gain", bands_[b].gain_db, 0.0f);
    if (!isShelf(b)) output.set(p + "Q", bands_[b].q, kSpecs[b].default_q);
  }
}
