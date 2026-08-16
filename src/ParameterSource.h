#ifndef _PARAMETERSOURCE_H_
#define _PARAMETERSOURCE_H_

#include <cmath>
#include <cstdlib>
#include <string>
#include <memory>
#include <unordered_map>

class ParameterSource {
 public:
  ParameterSource() { }
  ParameterSource(std::shared_ptr<std::unordered_map<std::string, int>> id_mapping) : id_mapping_(std::move(id_mapping)) { }
  virtual ~ParameterSource() { }
  
  virtual void set(const std::string & name, int value) = 0;
  virtual void set(const std::string & name, float value) = 0;
  virtual void set(const std::string & name, const std::string & value) = 0;

  // Deviation-only convenience: writes only when value differs from
  // default_value (float compared with a fixed epsilon, matching the
  // tolerance every caller of this idiom already used ad hoc - see
  // InstrumentTrack.cpp's sendA/sendB and, before this existed, the
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

  virtual bool has(const std::string & name) const = 0;
  virtual int getInt(const std::string & name, int default_value = 0) const = 0;
  virtual std::string getText(const std::string & name, const std::string & default_value) const = 0;
  virtual float getFloat(const std::string & name, float default_value = 0) const = 0;
  
  virtual bool getBool(const std::string & name, bool default_value = false) const {
    auto s = getText(name);
    if (s == "1" || s == "yes" || s == "true") return true;
    else if (s == "0" || s == "no" || s == "false") return false;
    else {
      return default_value;
    }
  }

  std::string getText(const std::string & name) const { return getText(name, ""); }

  int getInternalId(const std::string & name) {
    auto id = getText(name);
    if (!id.empty()) {
      auto it = id_mapping_->find(id);
      if (it != id_mapping_->end()) return it->second;
    }
    return 0;
  }

private:
  std::shared_ptr<std::unordered_map<std::string, int>> id_mapping_;
};

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
