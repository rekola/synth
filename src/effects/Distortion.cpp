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
  // (encodeMonoAsPoint() is a Main-only, directional concept - Aux is a
  // shared-bus scalar) - they've already been distorted below, same as
  // Main, and need to survive the re-encode to actually reach the bus.
  AudioBuffer reencodeIfNeeded(const ChannelConfiguration & channel_config, AudioBuffer data) const {
    if (channel_config.isMono()) return data;
    bool has_main = data.hasChannel(Channel::Main);
    AudioBuffer out(has_main ? channel_config.numberOfChannels() : 0,
		    data.hasChannel(Channel::AuxA), data.hasChannel(Channel::AuxB), data.numberOfFrames());
    out.zero();
    if (has_main) encodeMonoAsPoint(data, out);
    for (auto ch : { Channel::AuxA, Channel::AuxB }) {
      if (auto * src = data.getChannel(ch)) {
	auto dst = out.getChannel(ch);
	for (int i = 0; i < data.numberOfFrames(); i++) dst[i] = src[i];
      }
    }
    return out;
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
};

// Gathers children reduced to MONO (reduceForEffect), never raw ambisonic
// - panning doesn't survive under this nonlinear effect; see Distortion.h
// and the "Effects" section of the spatial audio plan for why this can't
// rely on TrackState's/VoiceState's generic children-gathering the way a
// transparent effect does.

class DistortionTrackState : public EffectTrackState {
public:
  DistortionTrackState(const ChannelConfiguration & channel_config, DistortionType type, float param, float drymix, float drive)
    : EffectTrackState(channel_config), dsp_(type, param, drymix, drive) { }

  AudioBuffer render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto data = renderChildren(frames, instruments, context, reduced_config);
    applyEffect(data);
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), std::move(data));
  }

protected:
  void applyEffect(AudioBuffer & input) override {
    setEffectActive(dsp_.applyEffect(input));
    setTrackInfo(TrackInfo( isEffectActive(), input.isClipping() ));
  }

private:
  DistortionDsp dsp_;
};

class DistortionVoiceState : public EffectVoiceState {
public:
  DistortionVoiceState(const ChannelConfiguration & channel_config, DistortionType type, float param, float drymix, float drive)
    : EffectVoiceState(channel_config), dsp_(type, param, drymix, drive) { }

  AudioBuffer render(int frames) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto data = renderChildren(frames, reduced_config);
    applyEffect(data);
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), std::move(data));
  }

protected:
  void applyEffect(AudioBuffer & input) override {
    setEffectActive(dsp_.applyEffect(input));
  }

private:
  DistortionDsp dsp_;
};

}

std::unique_ptr<TrackState>
Distortion::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<DistortionTrackState>(channel_config, type_, param_, drymix_, drive_);
}

std::unique_ptr<VoiceState>
Distortion::createVoiceState(const ChannelConfiguration & channel_config) const {
  return make_unique<DistortionVoiceState>(channel_config, type_, param_, drymix_, drive_);
}

void
Distortion::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);

  auto type_text = input.getText("type");
  if (type_text == "hardclip") type_ = DistortionType::HARD_CLIP;
  else if (type_text == "softclip") type_ = DistortionType::SOFT_CLIP;
  else if (type_text == "bitcrush") type_ = DistortionType::BITCRUSH;
  else if (type_text == "tanh") type_ = DistortionType::TANH;

  // BITCRUSH's own "param" is a bit depth, not a clip threshold (see
  // DistortionDsp::applyEffect()), so it needs its own default rather
  // than the clip types' implicit "unset means 0".
  param_ = input.getFloat("param", type_ == DistortionType::BITCRUSH ? 8.0f : 0.0f);
  drive_ = input.getFloat("drive", 1.0f);
}

void
Distortion::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("param", param_);
  output.set("drive", drive_);
  output.set("type", to_string(type_));
}
