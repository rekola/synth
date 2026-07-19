#ifndef _MIXERTYPE_H_
#define _MIXERTYPE_H_

#include <string>

enum class MixerType {
  BASIC = 1,
  AMBISONIC_STEREO,
  AMBISONIC_BINAURAL
};

static inline const std::string to_string(MixerType type) {
  switch (type) {
  case MixerType::BASIC: return "basic";
  case MixerType::AMBISONIC_STEREO: return "ambisonic_stereo";
  case MixerType::AMBISONIC_BINAURAL: return "ambisonic_binaural";
  default: return "unknown";
  }
}

#endif
