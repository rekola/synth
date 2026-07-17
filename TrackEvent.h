#ifndef _TRACKEVENT_H_
#define _TRACKEVENT_H_

#include "Command.h"

class TrackEvent {
 public:
  TrackEvent(short _id, float _frequency, float _velocity, int _note_value = -1)
    : id(_id), frequency(_frequency), velocity(_velocity), note_value(_note_value) { }

  short getId() const { return id; }

  bool isAftertouch() const { return frequency == 0.0f && velocity > 0.0f; }
  bool isOff() const { return velocity == 0.0f; }

  float getFrequency() const { return frequency; }
  float getVelocity() const { return velocity; }
  int getNoteValue() const { return note_value; }

 private:
  short id;
  float frequency, velocity;
  int note_value;
};

#endif
