#ifndef _SONGOBJECT_H_
#define _SONGOBJECT_H_

#include "ParameterSource.h"

#include <atomic>

class SongObject {
 public:
  SongObject() : id_(getNextId()) { }
  SongObject(int id) : id_(id != -1 ? id : getNextId()) { }
  SongObject(std::string name) : id_(getNextId()), name_(std::move(name)) { }
  virtual ~SongObject() { }
  
  int getId() const { return id_; }
  
  static int getNextId() {
    return next_id.fetch_add(1);
  }

  virtual void loadParameters(const ParameterSource & input) {
    id_ = input.getInt("id", -1);
    setName(input.getText("name"));
    setVolume(input.getFloat("volume", 1.0f));
  }

  virtual void storeParameters(ParameterSource & output) const {
    output.set("id", id_);
    if (!getName().empty()) output.set("name", getName());
    output.set("volume", getVolume());
  }

  float getVolume() const { return volume_; }
  void setVolume(float volume) { volume_ = volume; }

  void setName(std::string name) { name_ = std::move(name); }
  const std::string & getName() const { return name_; }

 private:
  int id_;
  std::string name_;
  float volume_ = 1.00f;

  static std::atomic<int> next_id;
};

#endif
