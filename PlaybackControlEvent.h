#ifndef _PLAYBACKCONTROLEVENT_H_
#define _PLAYBACKCONTROLEVENT_H_

#include "Event.h"

class PlaybackControlEvent : public Event {
 public:
  enum Type { PLAY = 1, STOP, MOVE_POSITION, CLEAR_VOICES, PLAY_NOTE, STOP_NOTE };
  
  PlaybackControlEvent(Type _type, int _parameter1 = 0, int _parameter2 = 0, int _parameter3 = 0)
    : type(_type), parameter1(_parameter1), parameter2(_parameter2), parameter3(_parameter3) { }

  void dispatch(EventHandler & evh) override { evh.handlePlaybackControlEvent(*this); }

  Type getType() const { return type; }
  int getParameter1() const { return parameter1; }
  int getParameter2() const { return parameter2; }
  int getParameter3() const { return parameter3; }
  
 private:
  Type type;
  int parameter1, parameter2, parameter3;
};

#endif
