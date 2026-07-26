#ifndef _MIXERTYPE_H_
#define _MIXERTYPE_H_

#include <string>

// AMBISONIC_BINAURAL has two underlying implementations - MagLS
// (AmbisonicMagLSDecoder, the default) and the older virtual-speaker rig
// (AmbisonicBinauralMixer, reachable via --legacy-binaural) - this stays a
// separate, orthogonal toggle (Controller::getUseLegacyBinaural()) rather
// than a third value here, since it isn't a distinct decode *concept* the
// way stereo vs. binaural is; it's an implementation choice within
// "binaural", and the request driving this decoder swap was explicit that
// it shouldn't grow into a richer settings surface than the existing
// stereo/binaural toggle already is.
enum class MixerType {
  AMBISONIC_STEREO = 1,
  AMBISONIC_BINAURAL
};

static inline const std::string to_string(MixerType type) {
  switch (type) {
  case MixerType::AMBISONIC_STEREO: return "ambisonic_stereo";
  case MixerType::AMBISONIC_BINAURAL: return "ambisonic_binaural";
  default: return "unknown";
  }
}

#endif
