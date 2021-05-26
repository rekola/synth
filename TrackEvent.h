#ifndef _TRACKEVENT_H_
#define _TRACKEVENT_H_

#include "Command.h"

class TrackEvent {
 public:
  enum Type { PLAY_NOTE,
	      AFTERTOUCH
  };
  TrackEvent(Type _type, short _id, float _delay, float _frequency, float _velocity)
    : type(_type), id(_id), delay(_delay), frequency(_frequency), velocity(_velocity) { }

  short getId() const { return id; }
  Type getType() const { return type; }
  
  bool isOff() const { return type == PLAY_NOTE && velocity == 0.0f; }
  float getDelay() const { return delay; }
  float getFrequency() const { return frequency; }
  float getVelocity() const { return velocity; }
  
 private:
  Type type;
  short id;
  float delay, frequency, velocity;
};

#endif
