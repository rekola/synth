#ifndef _LOGEVENT_H_
#define _LOGEVENT_H_

#include <string>

class LogEvent {
 public:
  explicit LogEvent(std::string text) : text_(std::move(text)) { }

  const std::string & getText() const { return text_; }

private:
  std::string text_;
};

#endif
