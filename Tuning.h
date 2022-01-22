#ifndef _TUNING_H_
#define _TUNING_H_

#include <string>

enum class Tuning {
  INHERIT = 0,
    TET12,
    TET19,
    TET31,
    TET53
};

static inline const std::string to_string(Tuning tuning) {
  switch (tuning) {
  case Tuning::INHERIT: return "inherit";
  case Tuning::TET12: return "12edo";
  case Tuning::TET19: return "19edo";
  case Tuning::TET31: return "31edo";
  case Tuning::TET53: return "53edo";
  default: return "";
  }
}

#endif
