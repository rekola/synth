#ifndef _TRACKEVENT_H_
#define _TRACKEVENT_H_

class TrackEvent {
 public:
  enum Type { PLAY_NOTE,
	      SET_VOLUME,
	      SET_EFFECT_PARAMETER
  };
  TrackEvent(short _id, float _delay, float _frequency, float _velocity)
    : id(_id), delay(_delay), frequency(_frequency), velocity(_velocity) { }

  short getId() const { return id; }
  
  bool isOff() const { return velocity == 0.0f; }
  float getDelay() const { return delay; }
  float getFrequency() const { return frequency; }
  float getVelocity() const { return velocity; }
  
 private:
  short id;
  float delay, frequency, velocity;
};

#endif
