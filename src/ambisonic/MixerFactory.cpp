#include "MixerFactory.h"

#include "AmbisonicDecoders.h"

#ifdef SYNTH_HAVE_LIBMYSOFA
#include "AmbisonicBinauralMixer.h"
#include "AmbisonicMagLSDecoder.h"
#endif

#include <fmt/core.h>

using namespace std;

// Every mixer ultimately decodes to a 2-channel stereo device signal via
// AmbisonicStereoMixer/AmbisonicBinauralMixer/AmbisonicMagLSDecoder - there
// is no separate plain-stereo-pan mixer anymore (BasicMixer was retired). A
// MONO (0th-order-ambisonic, W-only) config never attempts binaural
// decoding - there's no directional content to convolve - so it always
// falls straight through to the cardioid AmbisonicStereoMixer below, same
// as the AMBISONIC fallback path when no SOFA file resolves.
unique_ptr<Mixer>
createMixer(const ChannelConfiguration & channel_config, MixerType mixer_type, bool use_legacy_binaural) {
  auto sample_rate = channel_config.getAudioOutSampleRate();
  auto channels = channel_config.numberOfChannels(); // 1 (MONO), or 4/9/16 (AMBISONIC)

  if (channel_config.isAmbisonic()) {
#ifdef SYNTH_HAVE_LIBMYSOFA
    if (mixer_type == MixerType::AMBISONIC_BINAURAL) {
      if (use_legacy_binaural) {
        auto mixer = make_unique<AmbisonicBinauralMixer>(channels, sample_rate);
        if (mixer->isReady()) return mixer;
        fmt::print(stderr, "No SOFA file found for binaural decoding; falling back to plain stereo decode\n");
      } else {
        auto mixer = make_unique<AmbisonicMagLSDecoder>(channels, sample_rate);
        if (mixer->isReady()) return mixer;
        fmt::print(stderr, "No SOFA file found for binaural decoding; falling back to plain stereo decode\n");
      }
    }
#else
    if (mixer_type == MixerType::AMBISONIC_BINAURAL) {
      fmt::print(stderr, "Binaural decoding not compiled in (SYNTH_ENABLE_BINAURAL=OFF); falling back to plain stereo decode\n");
    }
#endif
  }

  return make_unique<AmbisonicStereoMixer>(channels, sample_rate);
}
