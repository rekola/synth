#ifndef _RECORDEVENT_H_
#define _RECORDEVENT_H_

#include "../audio/AudioBuffer.h"

class RecordEvent {
 public:
  RecordEvent(AudioBuffer data) : data_(std::move(data)) {}

  const AudioBuffer & getData() const { return data_; }
  
private:
  AudioBuffer data_;
};

#endif
