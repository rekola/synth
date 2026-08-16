#ifndef _TRACKEVENT_H_
#define _TRACKEVENT_H_

#include "../model/Command.h"
#include "../model/NoteCoordinate.h"

class TrackEvent {
 public:
  TrackEvent(short _id, float _frequency, float _velocity, int _note_value = -1, const NoteCoordinate & _note_coord = {})
    : id(_id), frequency(_frequency), velocity(_velocity), note_value(_note_value), note_coord(_note_coord) { }

  short getId() const { return id; }

  bool isAftertouch() const { return frequency == 0.0f && velocity > 0.0f; }
  bool isOff() const { return velocity == 0.0f; }

  float getFrequency() const { return frequency; }
  float getVelocity() const { return velocity; }
  int getNoteValue() const { return note_value; }
  const NoteCoordinate & getNoteCoordinate() const { return note_coord; }

 private:
  short id;
  float frequency, velocity;
  int note_value;
  NoteCoordinate note_coord;
};

#endif
