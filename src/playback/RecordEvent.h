#ifndef _RECORDEVENT_H_
#define _RECORDEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "../audio/AudioBuffer.h"

class RecordEvent : public Event {
public:
  RecordEvent(AudioBuffer data) : data_(std::move(data)) { }

  void dispatch(EventHandler & evh) override { evh.handleRecordEvent(*this); }
  
  const AudioBuffer & getData() const { return data_; }
  
private:
  AudioBuffer data_;
};

#endif
