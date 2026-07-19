#include "Distortion.h"

#include "EffectState.h"

using namespace std;

class DistortionState : public EffectState {
public:
  DistortionState(const ChannelConfiguration & channel_config, DistortionType type, float param, float drymix, float drive)
    : EffectState(channel_config), type_(type), param_(param), drymix_(drymix), drive_(drive) { }

  // Gathers children in real stereo/mono (reduceForEffect), never raw
  // ambisonic - see Distortion.h and the "Effects" section of the spatial
  // audio plan for why this nonlinear effect can't rely on TrackState's
  // generic children-gathering the way a transparent effect does.
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
  SampleData reencodeIfNeeded(SampleData data) {
    if (getChannelConfiguration().getType() != ChannelConfiguration::AMBISONIC) return data;
    SampleData out(getChannelConfiguration(), data.numberOfFrames());
    out.zero();
    encodeStereoAsPoints(data, out);
    if (!data.isZero()) out.setNonZero();
    return out;
  }

  void applyEffect(SampleData & input) override {
    if (!input.isZero()) {
      switch (type_) {
      case DistortionType::HARD_CLIP:
	for (int i = 0; i < input.numberOfChannels(); i++) {
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
	for (int i = 0; i < input.numberOfChannels(); i++) {
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
	  for (int i = 0; i < input.numberOfChannels(); i++) {
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

    setEffectActive(!input.isZero());
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
