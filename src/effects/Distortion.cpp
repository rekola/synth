#include "Distortion.h"

#include "EffectTrackState.h"
#include "EffectVoiceState.h"

using namespace std;

namespace {

// Actual DSP, shared by DistortionTrackState and DistortionVoiceState -
// see EffectTrackState.h/EffectVoiceState.h and
// plans/trackstate-voicestate-split.md.
class DistortionDsp {
public:
  DistortionDsp(DistortionType type, float param, float drymix, float drive)
    : type_(type), param_(param), drymix_(drymix), drive_(drive) { }

  // Aux channels are carried straight through, not spatially re-encoded
  // (a shared-bus scalar has no direction) - they've already been
  // distorted below, same as Main, and need to survive the re-encode to
  // actually reach the bus. Main re-encodes at a real point
  // (encodeMonoEffectAsPoint(), AmbisonicEncoding.h) - shared with
  // TapeDegradation, which needs exactly the same single-point treatment;
  // see MonoEffect.h's own class comment.
  AudioBuffer reencodeIfNeeded(const ChannelConfiguration & channel_config, const SphericalPosition & position, AudioBuffer data) {
    if (channel_config.isMono()) return data;
    return encodeMonoEffectAsPoint(channel_config, position, encoder_, std::move(data));
  }

  // Distorts every channel - Main and AuxA/AuxB alike, the same reasoning
  // as Amplifier/EnvelopeFilter/Compressor/Tremolo (the reverb/delay bus
  // shouldn't hear a bypassed-clean signal from a distorted source). NOTE:
  // this doesn't actually guarantee matching character between channels -
  // see docs/known_bugs.md - Main and Aux carry differently-scaled copies
  // of the same dry signal, and a nonlinear curve responds differently to
  // different amplitudes, so one can clip while the other stays clean.
  // Returns whether there was anything to distort this block, for the
  // caller's own isEffectActive() bookkeeping.
  bool applyEffect(AudioBuffer & input) const {
    int numChannels = input.numberOfChannels();
    if (numChannels > 0) {
      switch (type_) {
      case DistortionType::HARD_CLIP:
	for (int i = 0; i < numChannels; i++) {
	  auto buffer = input.getChannelData(i);
	  for (int j = 0; j < input.size(); j++) {
	    auto x = buffer[j];
	    auto y = drive_ * x;
	    if (y > param_) y = param_;
	    if (y < -param_) y = -param_;
	    buffer[j] = drymix_ * x + (1.0f - drymix_) * y;
	  }
	}
	break;

      case DistortionType::SOFT_CLIP:
	for (int i = 0; i < numChannels; i++) {
	  auto buffer = input.getChannelData(i);
	  for (int j = 0; j < input.size(); j++) {
	    auto x = buffer[j];
	    auto y = drive_ * x;
	    if (y > 1.0) y = 1.0;
	    else if (y < -1.0) y = -1.0;
	    y = y - y*y*y/3.0f;
	    y = 1.5 * y - 0.5 * y*y*y;
	    buffer[j] = drymix_ * x + (1.0f - drymix_) * y;
	  }
	}
	break;

      case DistortionType::TANH:
	{
	  float timbre = 1.0f;
	  float depth = 1.0f;
	  float timbreInverse = (1 - (timbre * 0.099)) * 10;
	  for (int i = 0; i < numChannels; i++) {
	    auto buffer = input.getChannelData(i);
	    for (int j = 0; j < input.size(); j++) {
	      auto x = buffer[j];
	      x *= depth;
	      x = tanhf(x * (timbre + 1));
	      x = x * ((0.1 + timbre) * timbreInverse);
	      x = cos((x + (timbre + 0.25)));
	      x = tanh(x * (timbre + 1));
	      x = x * 0.125;
	      buffer[j] = x;
	    }
	  }
	}
	break;

      case DistortionType::BITCRUSH:
	{
	  // Bit-depth reduction (quantizing each sample down to a coarse
	  // staircase of levels) - the classic lo-fi/chiptune "crushed"
	  // sound. param_ here is the target bit depth rather than the
	  // clip types' threshold, clamped since <1 would divide by zero
	  // and >24 is indistinguishable from no crushing at all at float
	  // precision. drive_ keeps its usual pre-gain-before-the-
	  // nonlinearity meaning, so a hot signal clips at the quantizer's
	  // +-1 rails for extra harmonic crunch, same as HARD_CLIP/SOFT_CLIP.
	  float bits = param_;
	  if (bits < 1.0f) bits = 1.0f;
	  else if (bits > 24.0f) bits = 24.0f;
	  float levels = powf(2.0f, bits - 1.0f);
	  for (int i = 0; i < numChannels; i++) {
	    auto buffer = input.getChannelData(i);
	    for (int j = 0; j < input.size(); j++) {
	      auto x = buffer[j];
	      auto y = drive_ * x;
	      if (y > 1.0f) y = 1.0f;
	      else if (y < -1.0f) y = -1.0f;
	      y = roundf(y * levels) / levels;
	      buffer[j] = drymix_ * x + (1.0f - drymix_) * y;
	    }
	  }
	}
	break;
      }
    }

    return numChannels > 0;
  }

private:
  DistortionType type_;
  float param_, drymix_, drive_;
  AmbisonicVoiceEncoder encoder_;
};

// Gathers children reduced to MONO (MonoEffect::getChildChannelConfiguration())
// - panning doesn't survive under this nonlinear effect; see Distortion.h
// for why this can't rely on TrackState's/VoiceState's generic
// children-gathering the way a transparent effect does.

class DistortionTrackState : public EffectTrackState {
public:
  DistortionTrackState(const ChannelConfiguration & channel_config, const SphericalPosition & position, DistortionType type, float param, float drymix, float drive)
    : EffectTrackState(channel_config), position_(position), dsp_(type, param, drymix, drive) { }

  AudioBuffer render(int frames, const InstrumentPool & instruments, RenderContext & context) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto data = renderChildren(frames, instruments, context, reduced_config);
    applyEffect(data);
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), position_, std::move(data));
  }

protected:
  void applyEffect(AudioBuffer & input) override {
    setEffectActive(dsp_.applyEffect(input));
    setTrackInfo(TrackInfo( isEffectActive(), input.isClipping() ));
  }

private:
  SphericalPosition position_;
  DistortionDsp dsp_;
};

class DistortionVoiceState : public EffectVoiceState {
public:
  DistortionVoiceState(const ChannelConfiguration & channel_config, const SphericalPosition & position, DistortionType type, float param, float drymix, float drive)
    : EffectVoiceState(channel_config), position_(position), dsp_(type, param, drymix, drive) { }

  AudioBuffer render(int frames) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto data = renderChildren(frames, reduced_config);
    applyEffect(data);
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), position_, std::move(data));
  }

  // A 0Hxx/0Kxx azimuth slide targeting the note this instance wraps
  // should still be audible through it - mirrors
  // TapeDegradationVoiceState::adjustAzimuth()'s identical reasoning.
  void adjustAzimuth(float delta) override {
    EffectVoiceState::adjustAzimuth(delta);
    position_.azimuth += delta;
  }

protected:
  void applyEffect(AudioBuffer & input) override {
    setEffectActive(dsp_.applyEffect(input));
  }

private:
  SphericalPosition position_;
  DistortionDsp dsp_;
};

}

std::unique_ptr<TrackState>
Distortion::createState(const ChannelConfiguration & channel_config, const SongStructure & structure) const {
  return make_unique<DistortionTrackState>(channel_config, getPosition(), type_, param_, drymix_, drive_);
}

std::unique_ptr<VoiceState>
Distortion::createVoiceState(const ChannelConfiguration & channel_config) const {
  // No position known through this path (see Track.h's own comment on
  // when it's actually reached) - the "no direction authored" sentinel,
  // same convention TapeDegradation::createVoiceState() uses.
  return make_unique<DistortionVoiceState>(channel_config, SphericalPosition{}, type_, param_, drymix_, drive_);
}

std::unique_ptr<VoiceState>
Distortion::playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune,
                      float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord, bool needs_decorrelation) const {
  // Mirrors Track::playNote()'s own default body (Track.h) exactly, except
  // the group node it builds is a real DistortionVoiceState (carrying the
  // note's real position) rather than the generic createVoiceState()-built
  // wrapper the default uses - same shape as TapeDegradation::playNote().
  auto group = make_unique<DistortionVoiceState>(config, position, type_, param_, drymix_, drive_);
  auto child_config = getChildChannelConfiguration(config);
  for (auto & child : getChildren()) {
    auto voice = child->playNote(child_config, position, frequency, detune, velocity, note_value, sends, note_coord, needs_decorrelation);
    if (voice.get()) group->addChild(child->getInternalId(), std::move(voice));
  }
  return group;
}

void
Distortion::loadParameters(const ParameterSource & input) {
  MonoEffect::loadParameters(input);

  auto type_text = input.get<std::string>("type");
  if (type_text == "hardclip") type_ = DistortionType::HARD_CLIP;
  else if (type_text == "softclip") type_ = DistortionType::SOFT_CLIP;
  else if (type_text == "bitcrush") type_ = DistortionType::BITCRUSH;
  else if (type_text == "tanh") type_ = DistortionType::TANH;

  // BITCRUSH's own "param" is a bit depth, not a clip threshold (see
  // DistortionDsp::applyEffect()), so it needs its own default rather
  // than the clip types' implicit "unset means 0".
  param_ = input.get<float>("param", type_ == DistortionType::BITCRUSH ? 8.0f : 0.0f);
  drive_ = input.get<float>("drive", 1.0f);
}

void
Distortion::storeParameters(ParameterSource & output) const {
  MonoEffect::storeParameters(output);

  output.set("param", param_);
  output.set("drive", drive_);
  output.set("type", to_string(type_));
}
