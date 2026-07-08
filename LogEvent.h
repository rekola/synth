#ifndef _LOGEVENT_H_
#define _LOGEVENT_H_

#include "Event.h"

#include <string>

class LogEvent : public Event {
 public:
  explicit LogEvent(std::string text) : text_(std::move(text)) { }

  void dispatch(EventHandler & evh) override { evh.handleLogEvent(*this); }

  const std::string & getText() const { return text_; }

private:
  std::string text_;
};

#endif
