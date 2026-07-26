#ifndef _MIXERFACTORY_H_
#define _MIXERFACTORY_H_

#include "ChannelConfiguration.h"
#include "MixerType.h"

#include <memory>

class Mixer;

// Shared by Player::createMixer (interactive playback) and
// OfflineRenderer/renderSongToWav (--render), so both exercise the exact
// same mixer-selection logic. `mixer_type` is a process-wide runtime
// setting (Controller::getMixerType()), not read from any Song - see
// MixerType.h for why. Ignored entirely unless `channel_config` is
// actually AMBISONIC: BasicMixer is used whenever it isn't, regardless of
// `mixer_type`. `use_legacy_binaural` (Controller::getUseLegacyBinaural())
// picks the older virtual-speaker-rig binaural decoder
// (AmbisonicBinauralMixer) over the default MagLS one
// (AmbisonicMagLSDecoder) - only consulted when `mixer_type` is actually
// AMBISONIC_BINAURAL.
std::unique_ptr<Mixer> createMixer(const ChannelConfiguration & channel_config, MixerType mixer_type, bool use_legacy_binaural = false);

#endif
