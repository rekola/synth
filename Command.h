#ifndef _COMMAND_H_
#define _COMMAND_H_

#include <string>
#include <string_view>
#include <cassert>

class Command {
 public:
  Command() {
    values_[0] = '-';
    values_[1] = '-';
    values_[2] = '-';
    values_[3] = '-';
  }

  Command(std::string_view values) {
    assert(values.size() == 4);
    if (values.size() >= 4) {
      values_[0] = values[0];
      values_[1] = values[1];
      values_[2] = values[2];
      values_[3] = values[3];
    }
  }

  void updateData(size_t i, char c) { if (i < 4) values_[i] = c; }
  
  bool isDefined() const { return values_[0] != '-' || values_[1] != '-' || values_[2] != '-' || values_[3] != '-'; }

  const char * data() const { return &(values_[0]); }
  
 private:
  char values_[4];
};

static inline const std::string to_string(const Command & command) {
  auto values = command.data();
  std::string s;
  s += values[0];
  s += values[1];
  s += values[2];
  s += values[3];
  return s;
}

#endif
