#ifndef _PARAMETERSOURCE_H_
#define _PARAMETERSOURCE_H_

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
  
  virtual bool has(const std::string & name) const = 0;
  virtual int getInt(const std::string & name, int default_value = 0) const = 0;
  virtual std::string getText(const std::string & name, const std::string & default_value) const = 0;
  virtual float getFloat(const std::string & name, float default_value = 0) const = 0;
  
  virtual bool getBool(const std::string & name) const { return getInt(name) != 0; }

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

#endif
