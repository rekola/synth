#ifndef _PARAMETERSOURCE_H_
#define _PARAMETERSOURCE_H_

#include <cmath>
#include <cstdlib>
#include <string>

class ParameterSource {
 public:
  ParameterSource() { }
  virtual ~ParameterSource() { }

  virtual void set(const std::string & name, int value) = 0;
  virtual void set(const std::string & name, float value) = 0;
  virtual void set(const std::string & name, const std::string & value) = 0;

  // Deviation-only convenience: writes only when value differs from
  // default_value (float compared with a fixed epsilon, matching the
  // tolerance every caller of this idiom already used ad hoc - see
  // LeafTrack.cpp's sendA/sendB and, before this existed, the
  // bus/-effect XML deviation checks it replaces). Non-virtual - built
  // once here on top of the pure virtual 2-argument set() above, so no
  // subclass needs its own copy of the epsilon or the comparison.
  void set(const std::string & name, int value, int default_value) {
    if (value != default_value) set(name, value);
  }
  void set(const std::string & name, float value, float default_value) {
    if (fabsf(value - default_value) > 0.0001f) set(name, value);
  }
  void set(const std::string & name, const std::string & value, const std::string & default_value) {
    if (value != default_value) set(name, value);
  }

  // No dedicated virtual of its own, same reasoning as get<bool>() below:
  // built on top of the string overload, so no subclass needs its own
  // bool-specific implementation.
  void set(const std::string & name, bool value) { set(name, std::string(value ? "true" : "false")); }
  void set(const std::string & name, bool value, bool default_value) {
    if (value != default_value) set(name, value);
  }

  virtual bool has(const std::string & name) const = 0;

  // Single typed accessor, replacing the old per-type getInt()/getFloat()/
  // getText()/getBool() quartet - specialized below for int/float/
  // std::string/bool, the only types anything in this codebase stores.
  // int/float/std::string forward straight to the per-type virtuals a
  // subclass implements (getIntImpl()/getFloatImpl()/getTextImpl()); bool
  // has no virtual of its own, it's derived from the text form the same
  // way getBool() always was. An unspecialized T fails to link rather than
  // silently doing the wrong thing.
  template <typename T> T get(const std::string & name, T default_value = T()) const;

 protected:
  virtual int getIntImpl(const std::string & name, int default_value) const = 0;
  virtual float getFloatImpl(const std::string & name, float default_value) const = 0;
  virtual std::string getTextImpl(const std::string & name, const std::string & default_value) const = 0;
};

template <> inline int ParameterSource::get<int>(const std::string & name, int default_value) const {
  return getIntImpl(name, default_value);
}
template <> inline float ParameterSource::get<float>(const std::string & name, float default_value) const {
  return getFloatImpl(name, default_value);
}
template <> inline std::string ParameterSource::get<std::string>(const std::string & name, std::string default_value) const {
  return getTextImpl(name, default_value);
}
template <> inline bool ParameterSource::get<bool>(const std::string & name, bool default_value) const {
  auto s = get<std::string>(name);
  if (s == "1" || s == "yes" || s == "true") return true;
  else if (s == "0" || s == "no" || s == "false") return false;
  else return default_value;
}

// Accepts either a plain decimal ("0.1875") or a fraction ("3/16") - both
// spellings of the same unit, the fraction form purely for hand-edited
// XML readability - a row-fraction/division attribute is always written
// back as a plain decimal (see the caller's own storeParameters()).
// Shared by bus/MultiTapDelay.cpp's baseRows and bus/Haze.h's predelay
// division, rather than each keeping its own copy - the two attributes
// mean different things (a continuous row-fraction vs. a snapped-to-one-
// of-three division) but parse the same textual shape.
inline float parseFraction(const std::string & text, float default_value) {
  if (text.empty()) return default_value;
  auto slash = text.find('/');
  if (slash == std::string::npos) return strtof(text.c_str(), nullptr);
  float numerator = strtof(text.substr(0, slash).c_str(), nullptr);
  float denominator = strtof(text.substr(slash + 1).c_str(), nullptr);
  return denominator != 0.0f ? numerator / denominator : default_value;
}

#endif
