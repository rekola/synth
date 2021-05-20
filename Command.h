#ifndef _COMMAND_H_
#define _COMMAND_H_

#include <string>

class Command {
 public:
  Command() {
    data[0] = '-';
    data[1] = '-';
    data[2] = '-';
    data[3] = '-';
  }

  Command(const char * _data) {
    data[0] = _data[0];
    data[1] = _data[1];
    data[2] = _data[2];
    data[3] = _data[3];
  }

  void updateData(size_t i, char c) { if (i < 4) data[i] = c; }
  
  bool isDefined() const { return data[0] != '-' || data[1] != '-' || data[2] != '-' || data[3] != '-'; }

  std::string toString() const {
    std::string s;
    s += data[0];
    s += data[1];
    s += data[2];
    s += data[3];
    return s;
  }
  
 private:
  char data[4];
};

#endif
