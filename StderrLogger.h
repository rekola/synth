#ifndef _STDERRLOGGER_H_
#define _STDERRLOGGER_H_

#include "Logger.h"

#include <cstdio>

class StderrLogger : public Logger {
 public:
  StderrLogger() { }

  void log(std::string s) override {
    fputs((s + "\n").c_str(), stderr);
    fflush(stderr);
  }
};

#endif
