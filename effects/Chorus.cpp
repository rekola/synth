#include "Chorus.h"

#include "../dsp/ChorusEngine.h"
#include "EffectTrackState.h"
#include "EffectVoiceState.h"

using namespace std;

namespace {

// Actual DSP, shared by ChorusTrackState and ChorusVoiceState - see
// EffectTrackState.h/EffectVoiceState.h and
// plans/trackstate-voicestate-split.md.
class ChorusDsp {
public:
  ChorusDsp(const ChannelConfiguration & channel_config, int voices, float rate, float delay, float depth, float mix)
    : engine_(reduceForEffect(channel_config).numberOfChannels(), channel_config.getAudioOutSampleRate(), voices, rate, delay, depth)
  {
    engine_.setMix(mix);
  }

  // engine_.process() runs unconditionally (Main and AuxA/AuxB alike, the
  // same reasoning as Amplifier/EnvelopeFilter/Compressor/Tremolo/
  // Distortion/BiquadFilter) - ChorusEngine itself already handles the
  // "some channels present, some not, some never seen" bookkeeping.
  void applyEffect(AudioBuffer & input_data) {
    engine_.process(input_data);
  }

  // Aux channels are carried straight through, not spatially re-encoded
  // (encodeMonoAsPoint() is a Main-only, directional concept - Aux is a
  // shared-bus scalar) - they've already been chorused above, same as
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

private:
  ChorusEngine engine_;
};

// Gathers children reduced to MONO (reduceForEffect), never raw ambisonic
// - same pattern as Distortion (effects/Distortion.cpp): the engine's
// per-channel state is sized once, at construction, from the reduced (now
// always 1) channel count, so its decorrelate=true option is what gives a
// mono-in source its stereo width, not panning surviving from children.

class ChorusTrackState : public EffectTrackState {
public:
  ChorusTrackState(const ChannelConfiguration & channel_config, int voices, float rate, float delay, float depth, float mix)
    : EffectTrackState(channel_config), dsp_(channel_config, voices, rate, delay, depth, mix) { }

  AudioBuffer render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto data = renderChildren(frames, instruments, context, reduced_config);
    applyEffect(data);
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), std::move(data));
  }

protected:
  void applyEffect(AudioBuffer & input_data) override {
    dsp_.applyEffect(input_data);
    setTrackInfo(TrackInfo( true, input_data.isClipping() ));
  }

private:
  ChorusDsp dsp_;
};

class ChorusVoiceState : public EffectVoiceState {
public:
  ChorusVoiceState(const ChannelConfiguration & channel_config, int voices, float rate, float delay, float depth, float mix)
    : EffectVoiceState(channel_config), dsp_(channel_config, voices, rate, delay, depth, mix) { }

  AudioBuffer render(int frames) override {
    auto reduced_config = reduceForEffect(getChannelConfiguration());
    auto data = renderChildren(frames, reduced_config);
    applyEffect(data);
    return dsp_.reencodeIfNeeded(getChannelConfiguration(), std::move(data));
  }

protected:
  void applyEffect(AudioBuffer & input_data) override {
    dsp_.applyEffect(input_data);
  }

private:
  ChorusDsp dsp_;
};

}

std::unique_ptr<TrackState>
Chorus::createState(const ChannelConfiguration & channel_config) const {
  return make_unique<ChorusTrackState>(channel_config, voices_, rate_, delay_, depth_, mix_);
}

std::unique_ptr<VoiceState>
Chorus::createVoiceState(const ChannelConfiguration & channel_config) const {
  return make_unique<ChorusVoiceState>(channel_config, voices_, rate_, delay_, depth_, mix_);
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
