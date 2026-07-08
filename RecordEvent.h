#ifndef _RECORDEVENT_H_
#define _RECORDEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "SampleData.h"

class RecordEvent : public Event {
public:
  RecordEvent(SampleData data) : data_(std::move(data)) { }

  void dispatch(EventHandler & evh) override { evh.handleRecordEvent(*this); }
  
  const SampleData & getData() const { return data_; }
  
private:
  SampleData data_;
};

#endif
