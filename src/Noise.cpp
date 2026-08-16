#include "Noise.h"

using namespace std;

#include "InstrumentVoice.h"
#include "WaveformType.h"
#include "NoiseColor.h"
#include "NoteCoordinate.h"
#include "dsp/HashField.h"
#include "dsp/NoiseGenerator.h"
#include "dsp/PinkNoiseFilter.h"

#include <vector>

namespace {
// Pairs a NoiseGenerator (white) with a PinkNoiseFilter (optional shaping) -
// without stealing samples from any HashField-derived value at audio rate.
class NoiseStream {
 public:
  explicit NoiseStream(uint32_t seed) : rng_(seed) { }

  float next(NoiseColor color) {
    float white = rng_.next();
    return color == NoiseColor::PINK ? pink_.process(white) : white;
  }

 private:
  NoiseGenerator rng_;
  PinkNoiseFilter pink_;
};

// Fixed compile-time seed, not per-instance - see InstrumentVoice.h's own
// kNotePhaseSalt for the identical reasoning (SoundFont.cpp's
// kPercussionJitterSeed/bus/GranularCloud.cpp's kDirectionScatterSeed
// precedent): the coordinate carries the per-voice variation, this salt
// just keeps NoiseVoice's own seed axis decorrelated from every other
// HashField-derived value the same note might draw.
constexpr uint64_t kNoiseSeedSalt = 0x7C3A9E5D2B481F63ull;
}

class NoiseVoice : public InstrumentVoice {
public:
  NoiseVoice(ChannelConfiguration config, const SphericalPosition & position, float level, NoiseColor color, const SendLevels & sends = {}, const NoteCoordinate & note_coord = {})
    : InstrumentVoice(config, position, 1.0f, sends, note_coord), level_(level), color_(color),
      noise_(seedFromCoord(note_coord)) {
  }

  AudioBuffer render(int frames) override {
    // No getDistanceGain() here - encodePosition() applies distance
    // attenuation itself now (see its own doc comment in InstrumentVoice.h).
    float gain = decibelsToGain(getGainDB()) * level_;

    if (static_cast<int>(dry_.size()) != frames) dry_.resize(static_cast<size_t>(frames));
    for (int k = 0; k < frames; k++) dry_[static_cast<size_t>(k)] = noise_.next(color_) * gain;

    return encodePosition(dry_.data(), frames);
  }

private:
  // One-time-per-voice seed, derived from this voice's own NoteCoordinate
  // (same one-time-per-note cost as InstrumentVoice's own start-phase
  // derivation) - deterministic and reproducible per note, safe to call
  // from the audio thread (HashField has no shared state at all).
  // Everything at audio rate afterwards comes from NoiseStream's own
  // NoiseGenerator, not HashField.
  static uint32_t seedFromCoord(const NoteCoordinate & note_coord) {
    return static_cast<uint32_t>(HashField(kNoiseSeedSalt).unit(note_coord.toHashCoord(), paramId("noise_seed")) * 4294967295.0f);
  }

  float level_;
  NoiseColor color_;
  NoiseStream noise_;
  std::vector<float> dry_;
};

std::unique_ptr<VoiceState>
Noise::playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord) const {
  auto voice = std::make_unique<NoiseVoice>(config, position, level_, color_, sends, note_coord);
  voice->playNote(frequency, velocity, note_value);
  return voice;
}
