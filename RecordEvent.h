#ifndef _RECORDEVENT_H_
#define _RECORDEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "SampleData.h"

class RecordEvent : public Event {
public:
  RecordEvent(const SampleData & _data) : data(_data) { }

  void dispatch(EventHandler & evh) override { evh.handleRecordEvent(*this); }
  
  const SampleData & getData() const { return data; }
  
private:
  SampleData data;
};

#endif
