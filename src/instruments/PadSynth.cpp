#include "PadSynth.h"

#include "PadSynthPresets.h"
#include "PadSynthVoice.h"

#include <cmath>

using namespace std;

std::unique_ptr<VoiceState>
PadSynth::playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord) const {
  // Unlike Oscillator, no child-forwarding loop here: PadSynthVoice's
  // render() has no modulator input point (it reads straight from the
  // wavetable), so forwarding children into it would just construct and
  // track voices whose output is never actually consumed.
  ensureWavetable(config, tuning);

  detune *= powf(2.0f, detune_cents_ / 1200.0f);

  auto voice = std::make_unique<PadSynthVoice>(config, position, detune, wavetable_, level_, sends, note_coord);
  voice->playNote(getFrequencyFor(tuning, note_value), velocity, note_value);
  return voice;
}

void
PadSynth::prewarm(const ChannelConfiguration & config, Tuning tuning, int note_value) const {
  ensureWavetable(config, tuning);
  // ensureWavetable() only constructs the (cheap) PadSynthTable
  // object itself - the actual expensive table generation happens lazily
  // inside its own getTable(f0), normally not called until a voice's
  // first render(). Force it now, for the region `note_value` falls in,
  // so that first render() finds it already built.
  wavetable_->getTable(getFrequencyFor(tuning, note_value) * powf(2.0f, detune_cents_ / 1200.0f));
}

void
PadSynth::ensureWavetable(const ChannelConfiguration & config, Tuning tuning) const {
  // (Re)built the first time this instrument is actually played (or
  // prewarm()ed), or if the output sample rate or the song's own tuning
  // has changed since - see this class's header comment for why prepare()
  // can't do this instead. N (edo_steps) comes from the engine's own
  // current tuning, not a hardcoded constant, so a tuning-matched table
  // always snaps to whatever scale the song is actually using.
  int edo_steps = edoStepsFor(tuning);
  if (!wavetable_ || wavetable_->getSampleRate() != config.getAudioOutSampleRate() || wavetable_tuning_ != tuning) {
    // PadSynthTable owns a full copy of the preset's own
    // oscillator/profile/position data - only the instance-level
    // overrides (tuning/remap/seed) come from this instrument's own XML
    // attributes.
    PadSynthParams params = getPadSynthPreset(preset_);
    params.edo_steps = edo_steps;
    params.tuning_matched = tuning_matched_;
    params.envelope_anchor_hz = envelope_anchor_hz_;
    params.envelope_tracking = envelope_tracking_;
    params.postprocess_kind = envelope_postprocess_ == "residue" ? SpectralPostprocessKind::ResidueClassWeighting
      : envelope_postprocess_ == "stretch" ? SpectralPostprocessKind::StretchMix
      : SpectralPostprocessKind::None;
    params.postprocess_n = envelope_postprocess_n_;
    params.postprocess_r = envelope_postprocess_r_;
    params.postprocess_amount = envelope_postprocess_amount_;
    params.seed = seed_;
    wavetable_ = std::make_shared<PadSynthTable>(config.getAudioOutSampleRate(), std::move(params));
    wavetable_tuning_ = tuning;
  }
}

void
PadSynth::loadParameters(const ParameterSource & input) {
  Instrument::loadParameters(input);

  preset_ = input.get<std::string>("preset", "strings");
  auto & preset = getPadSynthPreset(preset_);

  // Explicit attributes override their preset's own default, the same
  // "preset supplies defaults, an authored attribute always wins" shape
  // TapeDegradation::loadParameters() already uses.
  tuning_matched_ = input.get<bool>("tuningMatched", preset.tuning_matched);
  level_ = input.get<float>("level", 1.0f);
  detune_cents_ = input.get<float>("detune", 0.0f);
  seed_ = static_cast<uint64_t>(input.get<int>("seed", 1));

  envelope_anchor_hz_ = input.get<float>("envelopeAnchor", preset.envelope_anchor_hz);
  envelope_tracking_ = input.get<float>("envelopeTracking", preset.envelope_tracking);
  std::string default_postprocess = preset.postprocess_kind == SpectralPostprocessKind::ResidueClassWeighting ? "residue"
    : preset.postprocess_kind == SpectralPostprocessKind::StretchMix ? "stretch" : "";
  envelope_postprocess_ = input.get<std::string>("envelopePostprocess", default_postprocess);
  envelope_postprocess_n_ = input.get<int>("envelopePostprocessN", preset.postprocess_n);
  envelope_postprocess_r_ = input.get<int>("envelopePostprocessR", preset.postprocess_r);
  envelope_postprocess_amount_ = input.get<float>("envelopePostprocessAmount", preset.postprocess_amount);

  // Forces the next playNote() to rebuild the table against the new
  // parameters rather than keep serving whatever an earlier load (or
  // preset default) already cached.
  wavetable_.reset();
}

void
PadSynth::storeParameters(ParameterSource & output) const {
  Instrument::storeParameters(output);

  auto & preset = getPadSynthPreset(preset_);
  output.set("preset", preset_, std::string("strings"));
  output.set("tuningMatched", tuning_matched_, preset.tuning_matched);
  output.set("level", level_, 1.0f);
  output.set("detune", detune_cents_, 0.0f);
  output.set("seed", static_cast<int>(seed_), 1);

  output.set("envelopeAnchor", envelope_anchor_hz_, preset.envelope_anchor_hz);
  output.set("envelopeTracking", envelope_tracking_, preset.envelope_tracking);
  std::string default_postprocess = preset.postprocess_kind == SpectralPostprocessKind::ResidueClassWeighting ? "residue"
    : preset.postprocess_kind == SpectralPostprocessKind::StretchMix ? "stretch" : "";
  output.set("envelopePostprocess", envelope_postprocess_, default_postprocess);
  output.set("envelopePostprocessN", envelope_postprocess_n_, preset.postprocess_n);
  output.set("envelopePostprocessR", envelope_postprocess_r_, preset.postprocess_r);
  output.set("envelopePostprocessAmount", envelope_postprocess_amount_, preset.postprocess_amount);
}
