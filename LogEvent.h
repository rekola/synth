#ifndef _LOGEVENT_H_
#define _LOGEVENT_H_

#include "Event.h"

#include <string>

class LogEvent : public Event {
 public:
  explicit LogEvent(std::string _text) : text(_text) { }

  void dispatch(EventHandler & evh) override { evh.handleLogEvent(*this); }

  const std::string & getText() const { return text; }

private:
  std::string text;
};

#endif
