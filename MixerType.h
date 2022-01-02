#ifndef _MIXERTYPE_H_
#define _MIXERTYPE_H_

enum class MixerType {
  BASIC = 1,
    HRFT
};

static inline const std::string to_string(MixerType type) {
  switch (type) {
  case MixerType::BASIC: return "basic";
  case MixerType::HRFT: return "hrft";
  default: return "unknown";
  }
}

#endif
