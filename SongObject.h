#ifndef _SONGOBJECT_H_
#define _SONGOBJECT_H_

#include "ParameterSource.h"

#include <atomic>

class SongObject {
 public:
  SongObject() : internal_id_(getNextId()) { }
  SongObject(std::string name) : internal_id_(getNextId()), name_(std::move(name)) { }
  virtual ~SongObject() { }
  
  int getInternalId() const { return internal_id_; }
  
  static int getNextId() {
    return next_id.fetch_add(1);
  }

  virtual void loadParameters(const ParameterSource & input) {
    id_ = input.getText("id");
    setName(input.getText("name"));
    setVolume(input.getFloat("volume", 1.0f));
  }

  virtual void storeParameters(ParameterSource & output) const {
    if (!getId().empty()) output.set("id", id_);
    if (!getName().empty()) output.set("name", getName());
    if (getVolume() != 1.0f) output.set("volume", getVolume());
  }

  void setId(std::string id) { id_ = std::move(id); }
  const std::string & getId() const { return id_; }

  void setName(std::string name) { name_ = std::move(name); }
  const std::string & getName() const { return name_; }

  float getVolume() const { return volume_; }
  void setVolume(float volume) { volume_ = volume; }

 private:
  int internal_id_;
  std::string id_, name_;
  float volume_ = 1.00f;

  static std::atomic<int> next_id;
};

#endif
