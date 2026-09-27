#include "PadSynth.h"

#include "PadSynthPresets.h"
#include "PadSynthVoice.h"

using namespace std;

std::unique_ptr<VoiceState>
PadSynth::playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord, bool needs_decorrelation) const {
  // Unlike Oscillator, no child-forwarding loop here: PadSynthVoice's
  // render() has no modulator input point (it reads straight from the
  // wavetable), so forwarding children into it would just construct and
  // track voices whose output is never actually consumed.
  (void)needs_decorrelation;

  detune *= getHarmonic();
  detune /= getSubharmonic();

  // Lazily (re)built the first time this instrument is actually played, or
  // if the output sample rate or the song's own tuning has changed since -
  // see this class's header comment for why prepare() can't do this
  // instead. N (edo_steps) comes from the engine's own current tuning, not
  // a hardcoded constant, so a tuning-matched table always snaps to
  // whatever scale the song is actually using.
  int edo_steps = edoStepsFor(tuning);
  if (!wavetable_ || wavetable_->getSampleRate() != config.getAudioOutSampleRate() || wavetable_tuning_ != tuning) {
    auto & preset = getPadSynthPreset(preset_);
    wavetable_ = std::make_shared<PadSynthWavetable>(
      config.getAudioOutSampleRate(), partial_count_, bandwidth_cents_, bandwidth_scale_exponent_,
      edo_steps, partial_limit_, tuning_matched_, seed_,
      preset.amplitude_rolloff_exponent, preset.formants);
    wavetable_tuning_ = tuning;
  }

  auto voice = std::make_unique<PadSynthVoice>(config, position, detune, wavetable_, level_, sends, note_coord);
  voice->playNote(getFrequencyFor(tuning, note_value), velocity, note_value);
  return voice;
}

void
PadSynth::loadParameters(const ParameterSource & input) {
  Instrument::loadParameters(input);

  preset_ = input.get<std::string>("preset", "warm");
  auto & preset = getPadSynthPreset(preset_);

  // Explicit attributes override their preset's own default, the same
  // "preset supplies defaults, an authored attribute always wins" shape
  // TapeDegradation::loadParameters() already uses.
  bandwidth_cents_ = input.get<float>("bandwidth", preset.bandwidth_cents);
  bandwidth_scale_exponent_ = input.get<float>("bandwidthScale", preset.bandwidth_scale_exponent);
  partial_count_ = input.get<int>("partials", preset.partial_count);
  partial_limit_ = input.get<int>("partialLimit", 8);
  tuning_matched_ = input.get<bool>("tuningMatched", true);
  level_ = input.get<float>("level", 1.0f);
  seed_ = static_cast<uint64_t>(input.get<int>("seed", 1));

  // Forces the next playNote() to rebuild the table against the new
  // parameters rather than keep serving whatever an earlier load (or
  // preset default) already cached.
  wavetable_.reset();
}

void
PadSynth::storeParameters(ParameterSource & output) const {
  Instrument::storeParameters(output);

  auto & preset = getPadSynthPreset(preset_);
  output.set("preset", preset_, std::string("warm"));
  output.set("bandwidth", bandwidth_cents_, preset.bandwidth_cents);
  output.set("bandwidthScale", bandwidth_scale_exponent_, preset.bandwidth_scale_exponent);
  output.set("partials", partial_count_, preset.partial_count);
  output.set("partialLimit", partial_limit_, 8);
  output.set("tuningMatched", tuning_matched_, true);
  output.set("level", level_, 1.0f);
  output.set("seed", static_cast<int>(seed_), 1);
}
