#include "Chorus.h"

#include "../dsp/ChorusEngine.h"
#include "EffectState.h"

using namespace std;

class ChorusState : public EffectState {
public:
  ChorusState(const ChannelConfiguration & channel_config, int voices, float rate, float delay, float depth, float mix)
    : EffectState(channel_config),
      engine_(reduceForEffect(channel_config).numberOfChannels(), channel_config.getAudioOutSampleRate(), voices, rate, delay, depth)
  {
    engine_.setMix(mix);
  }

  // Gathers children reduced to MONO (reduceForEffect), never raw
  // ambisonic - same pattern as ReverbState (effects/Reverb.cpp): the
  // engine's per-channel state is sized once, at construction, from the
  // reduced (now always 1) channel count, so its decorrelate=true option
  // is what gives a mono-in source its stereo width, not panning surviving
  // from children.
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
  // shared-bus scalar) - they've already been chorused below, same as
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

  // engine_.process() runs unconditionally (Main and AuxA/AuxB alike, the
  // same reasoning as Amplifier/EnvelopeFilter/Compressor/Tremolo/
  // Distortion/BiquadFilter) - ChorusEngine itself already handles the
  // "some channels present, some not, some never seen" bookkeeping.
  void applyEffect(SampleData & input_data) override {
    engine_.process(input_data);
    setTrackInfo(TrackInfo( true, input_data.isClipping() ));
  }

private:
  ChorusEngine engine_;
};

std::unique_ptr<TrackState>
Chorus::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<ChorusState>(channel_config, voices_, rate_, delay_, depth_, mix_);
}

void
Chorus::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);

  voices_ = input.getInt("voices", 3);
  rate_ = input.getFloat("rate", 0.5f);
  delay_ = input.getFloat("delay", 15.0f);
  depth_ = input.getFloat("depth", 4.0f);
  mix_ = input.getFloat("mix", 0.5f);
}

void
Chorus::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("voices", voices_);
  output.set("rate", rate_);
  output.set("delay", delay_);
  output.set("depth", depth_);
  output.set("mix", mix_);
}
