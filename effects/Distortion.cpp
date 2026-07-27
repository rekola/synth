#include "Distortion.h"

#include "EffectState.h"

using namespace std;

class DistortionState : public EffectState {
public:
  DistortionState(const ChannelConfiguration & channel_config, DistortionType type, float param, float drymix, float drive)
    : EffectState(channel_config), type_(type), param_(param), drymix_(drymix), drive_(drive) { }

  // Gathers children reduced to MONO (reduceForEffect), never raw
  // ambisonic - panning doesn't survive under this nonlinear effect; see
  // Distortion.h and the "Effects" section of the spatial audio plan for
  // why this can't rely on TrackState's generic children-gathering the
  // way a transparent effect does.
  SampleData render(int frames) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto data = renderChildren(frames, reduced_config);
    applyEffect(data);
    return reencodeIfNeeded(std::move(data));
  }

  SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto data = renderChildren(frames, instruments, context, reduced_config);
    applyEffect(data);
    return reencodeIfNeeded(std::move(data));
  }

protected:
  // Aux channels are carried straight through, not spatially re-encoded
  // (encodeMonoAsPoint() is a Main-only, directional concept - Aux is a
  // shared-bus scalar) - they've already been distorted below, same as
  // Main, and need to survive the re-encode to actually reach the bus.
  SampleData reencodeIfNeeded(SampleData data) {
    if (getChannelConfiguration().isMono()) return data;
    bool has_main = data.hasChannel(Channel::Main);
    SampleData out(has_main ? getChannelConfiguration().numberOfChannels() : 0,
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
  void applyEffect(SampleData & input) override {
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
	break;
      }
    }

    setEffectActive(numChannels > 0);
    setTrackInfo(TrackInfo( isEffectActive(), input.isClipping()));
  }

private:
  DistortionType type_;
  float param_, drymix_, drive_;
};

std::unique_ptr<TrackState>
Distortion::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<DistortionState>(channel_config, type_, param_, drymix_, drive_);
}

void
Distortion::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);
   
  param_ = input.getFloat("param");
  drive_ = input.getFloat("drive", 1.0f);
  
  auto type_text = input.getText("type");
  if (type_text == "hardclip") type_ = DistortionType::HARD_CLIP;
  else if (type_text == "softclip") type_ = DistortionType::SOFT_CLIP;
  else if (type_text == "bitchrush") type_ = DistortionType::BITCRUSH;  
}

void
Distortion::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("param", param_);
  output.set("drive", drive_);
  output.set("type", to_string(type_));
}
