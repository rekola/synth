#ifndef _UI_H_
#define _UI_H_

#include <string>

class UI {
 public:
  explicit UI() { }
  virtual ~UI() { }

  virtual void setStatus(const std::string & s) = 0;
};

#endif
