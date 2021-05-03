#ifndef _TUNING_H_
#define _TUNING_H_

#include <string>

enum class Tuning {
  INHERIT = 0,
    TET5,
    TET7,
    TET12,
    TET19,
    TET31
};

static inline const std::string to_string(Tuning tuning) {
  switch (tuning) {
  case Tuning::INHERIT: return "inherit";
  case Tuning::TET5: return "5-TET";
  case Tuning::TET7: return "7-TET";
  case Tuning::TET12: return "12-TET";
  case Tuning::TET19: return "19-TET";
  case Tuning::TET31: return "31-TET";
  default: return "unknown";
  }
}

#endif
