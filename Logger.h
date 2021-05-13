#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <iostream>

class Logger {
 public:
  Logger() { }

  void log(std::string s) {
    std::cerr << s << "\n";
  }
};

#endif
