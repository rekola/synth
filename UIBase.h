#ifndef _UIBASE_H_
#define _UIBASE_H_

#include <string>

class UIBase {
 public:
  explicit UIBase() { }
  virtual ~UIBase() { }

  virtual void setStatus(const std::string & s) = 0;
};

#endif
