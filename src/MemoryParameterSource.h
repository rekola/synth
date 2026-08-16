#ifndef _MEMORYPARAMETERSOURCE_H_
#define _MEMORYPARAMETERSOURCE_H_

#include "ParameterSource.h"

#include <cstdlib>
#include <unordered_map>

// A plain in-memory ParameterSource, backed by a string-keyed map rather
// than an XML element - the same float/int-to-string round trip
// Song.cpp's own (file-local) XMLParameterSource uses, just without a
// tinyxml2::XMLElement underneath. Used wherever a SongObject's
// parameters need to be captured and replayed without ever touching XML:
// concretely, Song owns bus-slot BusEffect instances (bus/BusEffect.h)
// purely for their own loadParameters()/storeParameters() logic,
// constructed at an arbitrary placeholder sample rate since Song::open()
// has no access to the real device sample rate; SongState::initialize()
// round-trips a slot's parameters through one of these into a freshly
// constructed, correctly-sample-rated BusEffect, rather than needing
// per-type dispatch to copy fields itself.
class MemoryParameterSource : public ParameterSource {
 public:
  void set(const std::string & name, int value) override { values_[name] = std::to_string(value); }
  void set(const std::string & name, float value) override { values_[name] = std::to_string(value); }
  void set(const std::string & name, const std::string & value) override { values_[name] = value; }

  bool has(const std::string & name) const override { return values_.find(name) != values_.end(); }

  int getInt(const std::string & name, int default_value = 0) const override {
    auto it = values_.find(name);
    return it != values_.end() ? atoi(it->second.c_str()) : default_value;
  }
  std::string getText(const std::string & name, const std::string & default_value) const override {
    auto it = values_.find(name);
    return it != values_.end() ? it->second : default_value;
  }
  float getFloat(const std::string & name, float default_value = 0) const override {
    auto it = values_.find(name);
    return it != values_.end() ? strtof(it->second.c_str(), nullptr) : default_value;
  }

  // Nothing was ever set() on this instance - used to detect a fully
  // default-valued SongObject (deviation-only storeParameters() writes
  // nothing at all in that case) without needing per-type knowledge of
  // what "default" means (see Song.cpp's busSlotIsDefault()).
  bool isEmpty() const { return values_.empty(); }

 private:
  std::unordered_map<std::string, std::string> values_;
};

#endif
