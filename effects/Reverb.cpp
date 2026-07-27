#include "Reverb.h"

#include "MVerb.h"
#include "EffectState.h"

using namespace std;

class ReverbState : public EffectState {
public:
  ReverbState(const ChannelConfiguration & channel_config, float damping_freq, float density, float bandwidth_freq, float decay, float predelay, float size, float gain, float mix, float earlymix, bool bpm_lock)
    : EffectState(channel_config),
      predelay_(predelay),
      bpm_lock_(bpm_lock) {
    mverb_.setParameter(MVerb<float>::DAMPINGFREQ, damping_freq);
    mverb_.setParameter(MVerb<float>::DENSITY, density);
    mverb_.setParameter(MVerb<float>::BANDWIDTHFREQ, bandwidth_freq);
    mverb_.setParameter(MVerb<float>::DECAY, decay);
    mverb_.setParameter(MVerb<float>::PREDELAY, predelay);
    mverb_.setParameter(MVerb<float>::SIZE, size);
    mverb_.setParameter(MVerb<float>::GAIN, gain);
    mverb_.setParameter(MVerb<float>::MIX, mix);
    mverb_.setParameter(MVerb<float>::EARLYMIX, earlymix);
    mverb_.setSampleRate(channel_config.getAudioOutSampleRate());
  }

  // Gathers children reduced to MONO (reduceForEffect), never raw
  // ambisonic - a nonlinear effect like this one gets dedicated, isolated
  // DSP state either way; MVerb always duplicates the single reduced
  // channel into its own stereo processing (see applyEffect() below)
  // rather than getting real stereo width from panned children, which is
  // no longer preserved under this effect. Re-encodes
  // back up to this node's own true format afterward if that's actually
  // ambisonic (encodeMonoAsPoint is a no-op cost-wise otherwise, since
  // reduced == true format and this branch is skipped) - the re-encoded
  // signal is necessarily non-directional (W only), matching the mono
  // input it came from. See the "Effects" section of the spatial audio
  // plan for why this can't just rely on TrackState's generic children
  // gathering the way every other (transparent) effect does.
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
  // Aux channels (if data has any - see SampleData.h) are deliberately
  // dropped here, not carried through like the other effects now do:
  // AuxA/AuxB already *are* the shared reverb/delay bus's own input, so
  // running this per-track reverb on top of them too would be reverbing
  // the reverb send itself, not a meaningful operation.
  SampleData reencodeIfNeeded(SampleData data) {
    if (getChannelConfiguration().isMono()) return data;
    bool has_main = data.hasChannel(Channel::Main);
    SampleData out(has_main ? getChannelConfiguration().numberOfChannels() : 0, false, false, data.numberOfFrames());
    out.zero();
    if (has_main) encodeMonoAsPoint(data, out);
    return out;
  }

  void applyEffect(SampleData & input) override {
    if (bpm_lock_) {
      mverb_.setParameter(MVerb<float>::PREDELAY, predelay_ * getChannelConfiguration().getRowDuration(input.getBpm()));
    }

    // Presence guard: input.getChannelData(0) below is only safe once
    // Main actually exists (a voice with Send Main = 0 allocates none -
    // see SampleData.h). Deliberately not also fixing the `isEffectActive()`
    // latch below (never reset to false, so this keeps "running" forever
    // after the first real input) - out of scope, Reverb (built on
    // GPL-licensed MVerb) is slated for removal separately regardless.
    if (input.hasChannel(Channel::Main)) {
      setEffectActive(true);

      auto left_out_ptr = unique_ptr<float[]>(new float[input.size()]);
      auto right_out_ptr = unique_ptr<float[]>(new float[input.size()]);
      auto left_out = left_out_ptr.get(), right_out = right_out_ptr.get();

      memset(left_out, 0, input.size() * sizeof(float));
      memset(right_out, 0, input.size() * sizeof(float));

      float * out[2] = { left_out, right_out };

      // Always duplicate the single reduced (see reduceForEffect) Main
      // channel into MVerb's stereo input and write back only to it -
      // the old input.numberOfChannels()==2 branch that treated a raw
      // 2-channel input as genuine stereo Main is removed: reduceForEffect
      // always leaves exactly 1 Main channel, so that branch only ever
      // actually triggered on a 1 Main + 1 Aux buffer, misreading Aux as a
      // second Main channel.
      float * in[2] = { input.getChannelData(0), input.getChannelData(0) };

      mverb_.process(in, out, input.size());

      auto left_buffer = input.getChannelData(0);
      for (int i = 0; i < input.size(); i++) {
	left_buffer[i] = left_out[i];
      }
    }

    setTrackInfo(TrackInfo( isEffectActive(), input.isClipping()));
  }

private:
  MVerb<float> mverb_;
  float predelay_;
  bool bpm_lock_;
};

std::unique_ptr<TrackState>
Reverb::createState(const ChannelConfiguration & channel_config) const {  
  return make_unique<ReverbState>(channel_config, damping_freq_, density_, bandwidth_freq_, decay_, predelay_, size_, gain_, mix_, earlymix_, bpm_lock_);
}

void
Reverb::loadParameters(const ParameterSource & input) {
  Effect::loadParameters(input);
  
  auto preset_text = input.getText("preset");
  if (preset_text == "subtle") preset_ = ReverbPreset::SUBTLE;
  else if (preset_text == "stadium") preset_ = ReverbPreset::STADIUM;
  else if (preset_text == "cupboard") preset_ = ReverbPreset::CUPBOARD;
  else if (preset_text == "dark") preset_ = ReverbPreset::DARK;
  else if (preset_text == "halves") preset_ = ReverbPreset::HALVES;
  else preset_ = ReverbPreset::NONE;
  
  switch (preset_) {
  case ReverbPreset::NONE:
    break;
  case ReverbPreset::SUBTLE:
    damping_freq_ = 0.0f;
    density_ = 0.5f;
    bandwidth_freq_ = 1.0f;
    decay_ = 0.5f;
    predelay_ = 0.0f;
    size_ = 0.5f;
    gain_ = 1.0f;
    mix_ = 0.15f;
    earlymix_ = 0.75f;
    break;
  case ReverbPreset::STADIUM:
    damping_freq_ = 0.0f;
    density_ = 0.5f;
    bandwidth_freq_ = 1.0f;
    decay_ = 0.5f;
    predelay_ = 0.0f;
    size_ = 1.0f;
    mix_ = 0.35f;
    earlymix_ = 0.75f;
    break;
  case ReverbPreset::CUPBOARD:
    damping_freq_ = 0.0f;
    density_ = 0.5f;
    bandwidth_freq_ = 1.0f;
    decay_ = 0.5f;
    predelay_ = 0.0f;
    size_ = 0.25f;
    gain_ = 1.0f;
    mix_ = 0.35f;
    earlymix_ = 0.75f;
    break;
  case ReverbPreset::DARK:
    damping_freq_ = 0.9f;
    density_ = 0.5f;
    bandwidth_freq_ = 0.1f;
    decay_ = 0.5f;
    predelay_ = 0.0f;
    size_ = 0.5f;
    gain_ = 1.0f;
    mix_ = 0.5f;
    earlymix_ = 0.75f;
    break;
  case ReverbPreset::HALVES:
    damping_freq_ = 0.5f;
    density_ = 0.5f;
    bandwidth_freq_ = 0.5f;
    decay_ = 0.5f;
    predelay_ = 0.5f;
    gain_ = 1.0f;
    mix_ = 0.5f;
    earlymix_ = 0.5f;
    size_ = 0.75f;
    break;
  }

  predelay_ = input.getFloat("predelay", predelay_);
  bpm_lock_ = input.getBool("lock", false);
}

void
Reverb::storeParameters(ParameterSource & output) const {
  Effect::storeParameters(output);

  output.set("preset", to_string(preset_));
  output.set("lock", bpm_lock_);
}
