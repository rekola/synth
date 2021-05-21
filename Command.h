#ifndef _COMMAND_H_
#define _COMMAND_H_

#include <string>

class Command {
 public:
  Command() {
    values[0] = '-';
    values[1] = '-';
    values[2] = '-';
    values[3] = '-';
  }

  Command(const char * _values) {
    values[0] = _values[0];
    values[1] = _values[1];
    values[2] = _values[2];
    values[3] = _values[3];
  }

  void updateData(size_t i, char c) { if (i < 4) values[i] = c; }
  
  bool isDefined() const { return values[0] != '-' || values[1] != '-' || values[2] != '-' || values[3] != '-'; }

  std::string toString() const {
    std::string s;
    s += values[0];
    s += values[1];
    s += values[2];
    s += values[3];
    return s;
  }

  const char * data() const { return &(values[0]); }
  
 private:
  char values[4];
};

#endif
