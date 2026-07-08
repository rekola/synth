#ifndef _PLAYBACKCONTROLEVENT_H_
#define _PLAYBACKCONTROLEVENT_H_

#include "Event.h"
#include "EventHandler.h"

class PlaybackControlEvent : public Event {
 public:
  enum Type { PLAY = 1, STOP, TERMINATE, MOVE_POSITION, CLEAR_VOICES, PLAY_NOTE, STOP_NOTE, NOTE_PRESSURE };
  
  PlaybackControlEvent(Type _type, int _parameter1 = 0, int _parameter2 = 0, int _parameter3 = 0, int _parameter4 = 0)
    : type(_type), parameter1(_parameter1), parameter2(_parameter2), parameter3(_parameter3), parameter4(_parameter4) { }

  void dispatch(EventHandler & evh) override { evh.handlePlaybackControlEvent(*this); }

  Type getType() const { return type; }
  int getParameter1() const { return parameter1; }
  int getParameter2() const { return parameter2; }
  int getParameter3() const { return parameter3; }
  int getParameter4() const { return parameter4; }
  
 private:
  Type type;
  int parameter1, parameter2, parameter3, parameter4;
};

#endif
