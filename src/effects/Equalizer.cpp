#include "Equalizer.h"

#include "EffectTrackState.h"
#include "EffectVoiceState.h"

#include "../dsp/Biquad.h"
#include "../dsp/ChannelBank.h"
#include "../util/constants.h"
#include "../state/RenderContext.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <string>
#include <vector>

using namespace std;

namespace {

Equalizer::Band defaultBand(FilterType type, float freq, float q, bool on) {
  Equalizer::Band band;
  band.type = type;
  band.freq = freq;
  band.q = q;
  band.on = on;
  return band;
}

FilterType parseType(const string & text, FilterType fallback) {
  for (auto type : { FilterType::lowpass, FilterType::highpass, FilterType::bandpass, FilterType::notch, FilterType::peak, FilterType::lowshelf, FilterType::highshelf }) {
    if (to_string(type) == text) return type;
  }
  return fallback;
}

Biquad<double>::Coefficients coefficientsOf(const Equalizer::Band & band, double sample_rate) {
  return Biquad<double>(band.type, band.freq / sample_rate, band.q, band.gain_db).coefficients();
}

// Shared by the track-tree and per-note states. Slot layout as in
// BiquadFilter: Main channels first, then AuxA and AuxB in their own slots.
// One bank per band, so a band's settings can change while it plays.
class EqualizerDsp {
public:
  EqualizerDsp(const ChannelConfiguration & channel_config, const Equalizer & eq)
    : main_slots_(channel_config.numberOfChannels()), sample_rate_(channel_config.getAudioOutSampleRate())
  {
    for (int b = 0; b < Equalizer::kBands; b++) {
      banks_.emplace_back(main_slots_ + 2, dsp::BiquadBank::Coefficients {});
      bands_[b] = eq.getBand(b);
      configure(b);
    }
  }

  // Follows `eq` if its bands differ from the ones in use.
  void update(const Equalizer & eq) {
    for (int b = 0; b < Equalizer::kBands; b++) {
      if (eq.getBand(b) == bands_[b]) continue;
      bool was_active = bands_[b].isActive();
      bands_[b] = eq.getBand(b);
      configure(b, !was_active);
    }
  }

  bool applyEffect(AudioBuffer & input) {
    if (input.numberOfChannels() == 0) return false;

    int main_channels = input.regularChannelCount();
    if (main_channels > main_slots_) main_channels = main_slots_;
    float * main_planes[dsp::kMaxBankChannels] = {};
    for (int c = 0; c < main_channels; c++) main_planes[c] = input.getChannelData(c);
    float * aux_planes[2] = { input.getChannel(Channel::AuxA), input.getChannel(Channel::AuxB) };

    auto numSamples = input.size();
    size_t offset = 0;
    while (numSamples) {
      int block = numSamples > constants::RENDER_EFFECTSAMPLEBLOCK ? constants::RENDER_EFFECTSAMPLEBLOCK : numSamples;
      for (int b = 0; b < Equalizer::kBands; b++) {
        if (!bands_[b].isActive()) continue;
        auto & bank = banks_[static_cast<size_t>(b)];
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
  void configure(int b, bool fresh = false) {
    auto & bank = banks_[static_cast<size_t>(b)];
    if (fresh) bank.clearState();
    if (!bands_[b].isActive()) return;
    auto c = coefficientsOf(bands_[b], sample_rate_);
    bank.setCoefficients({ c.a0, c.a1, c.a2, c.b1, c.b2 });
  }

  int main_slots_;
  double sample_rate_;
  Equalizer::Band bands_[Equalizer::kBands];
  vector<dsp::BiquadBank> banks_;
};

class EqualizerTrackState : public EffectTrackState {
public:
  EqualizerTrackState(const ChannelConfiguration & config, const Equalizer & eq)
    : EffectTrackState(config), id_(eq.getInternalId()), dsp_(config, eq) { }

  // The settings are edited while the song plays: each block looks up this
  // effect in the published tracks and follows any change.
  AudioBuffer render(int frames, const InstrumentPool & instruments, RenderContext & context) override {
    if (auto master = context.getMasterTrack()) {
      if (auto eq = dynamic_cast<const Equalizer *>(master->getChildByInternalId(id_))) dsp_.update(*eq);
    }
    return EffectTrackState::render(frames, instruments, context);
  }

protected:
  void applyEffect(AudioBuffer & input) override {
    setEffectActive(dsp_.applyEffect(input));
    setTrackInfo(TrackInfo( isEffectActive(), input.isClipping() ));
  }

private:
  int id_;
  EqualizerDsp dsp_;
};

class EqualizerVoiceState : public EffectVoiceState {
public:
  EqualizerVoiceState(const ChannelConfiguration & config, const Equalizer & eq)
    : EffectVoiceState(config), dsp_(config, eq) { }

protected:
  void applyEffect(AudioBuffer & input) override { setEffectActive(dsp_.applyEffect(input)); }

private:
  EqualizerDsp dsp_;
};

string key(int band, const char * name) { return "b" + to_string(band + 1) + name; }

}

Equalizer::Equalizer() {
  bands_[0] = defaultBand(FilterType::highpass, 30.0f, 0.707f, false);
  bands_[1] = defaultBand(FilterType::lowshelf, 100.0f, 0.707f, true);
  bands_[2] = defaultBand(FilterType::peak, 250.0f, 1.0f, true);
  bands_[3] = defaultBand(FilterType::peak, 800.0f, 1.0f, true);
  bands_[4] = defaultBand(FilterType::peak, 2500.0f, 1.0f, true);
  bands_[5] = defaultBand(FilterType::peak, 6000.0f, 1.0f, true);
  bands_[6] = defaultBand(FilterType::highshelf, 10000.0f, 0.707f, true);
  bands_[7] = defaultBand(FilterType::lowpass, 18000.0f, 0.707f, false);
}

void
Equalizer::setBand(int i, Band band) {
  band.freq = clamp(band.freq, kMinFreq, kMaxFreq);
  band.gain_db = clamp(band.gain_db, kMinGainDb, kMaxGainDb);
  band.q = clamp(band.q, kMinQ, kMaxQ);
  bands_[i] = band;
}

float
Equalizer::responseDb(float hz, float sample_rate) const {
  // |H(e^jw)| of (a0 + a1 z^-1 + a2 z^-2) / (1 + b1 z^-1 + b2 z^-2), per band.
  const double w = 2.0 * M_PI * static_cast<double>(hz) / static_cast<double>(sample_rate);
  const complex<double> z1 = polar(1.0, -w), z2 = polar(1.0, -2.0 * w);
  double db = 0.0;
  for (auto & band : bands_) {
    if (!band.isActive()) continue;
    auto c = coefficientsOf(band, sample_rate);
    auto h = (c.a0 + c.a1 * z1 + c.a2 * z2) / (1.0 + c.b1 * z1 + c.b2 * z2);
    db += 20.0 * log10(max(abs(h), 1e-9));
  }
  return static_cast<float>(db);
}

unique_ptr<TrackState>
Equalizer::createState(const ChannelConfiguration & config, const SongStructure &) const {
  return make_unique<EqualizerTrackState>(config, *this);
}

unique_ptr<VoiceState>
Equalizer::createVoiceState(const ChannelConfiguration & config) const {
  return make_unique<EqualizerVoiceState>(config, *this);
}

void
Equalizer::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);
  Equalizer defaults;
  for (int b = 0; b < kBands; b++) {
    auto & d = defaults.bands_[b];
    Band band;
    band.type = parseType(input.get<string>(key(b, "Type"), to_string(d.type)), d.type);
    band.freq = input.get<float>(key(b, "Freq"), d.freq);
    band.gain_db = input.get<float>(key(b, "Gain"), d.gain_db);
    band.q = input.get<float>(key(b, "Q"), d.q);
    band.on = input.get<bool>(key(b, "On"), d.on);
    setBand(b, band);
  }
}

void
Equalizer::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);
  Equalizer defaults;
  for (int b = 0; b < kBands; b++) {
    auto & band = bands_[b];
    auto & d = defaults.bands_[b];
    output.set(key(b, "Type"), to_string(band.type), to_string(d.type));
    output.set(key(b, "Freq"), band.freq, d.freq);
    output.set(key(b, "Gain"), band.gain_db, d.gain_db);
    output.set(key(b, "Q"), band.q, d.q);
    output.set(key(b, "On"), band.on, d.on);
  }
}
