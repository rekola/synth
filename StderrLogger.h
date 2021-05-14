#ifndef _STDERRLOGGER_H_
#define _STDERRLOGGER_H_

#include "Logger.h"

#include <iostream>

class StderrLogger : public Logger {
 public:
  StderrLogger() { }

  void log(std::string s) override {
    std::cerr << s << "\n";
  }
};

#endif
