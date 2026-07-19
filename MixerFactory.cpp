#include "MixerFactory.h"

#include "BasicMixer.h"
#include "AmbisonicDecoders.h"

#ifdef SYNTH_HAVE_LIBMYSOFA
#include "AmbisonicBinauralMixer.h"
#endif

#include <fmt/core.h>

using namespace std;

unique_ptr<Mixer>
createMixer(const ChannelConfiguration & channel_config, MixerType mixer_type) {
  auto sample_rate = channel_config.getAudioOutSampleRate();

  if (channel_config.getType() != ChannelConfiguration::AMBISONIC) {
    return make_unique<BasicMixer>(static_cast<short>(channel_config.numberOfChannels()), sample_rate);
  }

#ifdef SYNTH_HAVE_LIBMYSOFA
  if (mixer_type == MixerType::AMBISONIC_BINAURAL) {
    auto mixer = make_unique<AmbisonicBinauralMixer>(channel_config.numberOfChannels(), sample_rate);
    if (mixer->isReady()) return mixer;
    fmt::print(stderr, "No SOFA file found for binaural decoding; falling back to plain stereo decode\n");
  }
#else
  if (mixer_type == MixerType::AMBISONIC_BINAURAL) {
    fmt::print(stderr, "Binaural decoding not compiled in (SYNTH_ENABLE_BINAURAL=OFF); falling back to plain stereo decode\n");
  }
#endif

  return make_unique<AmbisonicStereoMixer>(channel_config.numberOfChannels(), sample_rate);
}
