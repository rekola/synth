#ifndef _LAUNCHPADBUTTONEVENT_H_
#define _LAUNCHPADBUTTONEVENT_H_

#include "../playback/Event.h"
#include "../playback/EventHandler.h"
#include "LaunchpadProtocol.h"

// A press/release on one of the extra round/CC buttons outside the 8x8
// pad grid (top row, right column, and on Pro MK3 also a left column and
// two rows below the grid). Mirrors LaunchpadPadEvent's shape.
class LaunchpadButtonEvent : public Event {
 public:
  enum Kind { PRESS, RELEASE };

  LaunchpadButtonEvent(int _device_index, int _cc_number, Kind _kind, LaunchpadProtocol::Model _model)
    : device_index(_device_index), cc_number(_cc_number), kind(_kind), model(_model) { }

  void dispatch(EventHandler & evh) override { evh.handleLaunchpadButtonEvent(*this); }

  int getDeviceIndex() const { return device_index; }
  int getCCNumber() const { return cc_number; }
  Kind getKind() const { return kind; }
  LaunchpadProtocol::Model getModel() const { return model; }

 private:
  int device_index, cc_number;
  Kind kind;
  LaunchpadProtocol::Model model;
};

#endif
