#ifndef _FILTERTYPE_H_
#define _FILTERTYPE_H_

enum class FilterType {
  lowpass = 0,
  highpass,
  bandpass,
  notch,
  peak,
  lowshelf,
  highshelf
};

static inline std::string to_string(FilterType type) {
  switch (type) {
  case FilterType::lowpass: return "lowpass";
  case FilterType::highpass: return "highpass";
  case FilterType::bandpass: return "bandpass";
  case FilterType::notch: return "notch";
  case FilterType::peak: return "peak";
  case FilterType::lowshelf: return "lowshelf";
  case FilterType::highshelf: return "highshelf";
  default: return "";
  }
}

#endif
