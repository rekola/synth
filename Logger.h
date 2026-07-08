#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <string>

class Logger {
 public:
  Logger() { }
  virtual ~Logger() { }
  
  virtual void log(std::string s) = 0;
};

#endif
