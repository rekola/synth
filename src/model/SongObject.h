#ifndef _SONGOBJECT_H_
#define _SONGOBJECT_H_

#include "../state/ParameterSource.h"

#include <atomic>

class SongObject {
 public:
  SongObject() : internal_id_(getNextId()) { }
  virtual ~SongObject() { }
  
  int getInternalId() const { return internal_id_; }
  
  virtual void loadParameters(const ParameterSource & input) {
    id_ = input.getText("id");
    setName(input.getText("name"));
  }

  virtual void storeParameters(ParameterSource & output) const {
    if (!getId().empty()) output.set("id", id_);
    if (!getName().empty()) output.set("name", getName());
  }

  void setId(std::string id) { id_ = std::move(id); }
  const std::string & getId() const { return id_; }

  void setName(std::string name) { name_ = std::move(name); }
  const std::string & getName() const { return name_; }

protected:
  static int getNextId() { return next_id.fetch_add(1); }

 private:
  int internal_id_;
  std::string id_, name_;

  static std::atomic<int> next_id;
};

#endif
