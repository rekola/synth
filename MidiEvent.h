#ifndef _MIDIEVENT_H_
#define _MIDIEVENT_H_

#include "Event.h"
#include "EventHandler.h"

class MidiEvent : public Event {
 public:
  enum Type { NOTE_ON, NOTE_OFF, NOTE_PRESSURE };
  
  MidiEvent(Type _type, short _note, short _velocity) : type(_type), note(_note), velocity(_velocity) { }

  void dispatch(EventHandler & evh) override { evh.handleMidiEvent(*this); }

  Type getType() const { return type; }
  short getNote() const { return note; }
  short getVelocity() const { return velocity; }
  
 private:
  Type type;
  short note, velocity;
};

#endif

