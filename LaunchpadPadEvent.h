#ifndef _LAUNCHPADPADEVENT_H_
#define _LAUNCHPADPADEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "LaunchpadProtocol.h"

class LaunchpadPadEvent : public Event {
 public:
  enum Kind { PRESS, RELEASE, AFTERTOUCH };

  LaunchpadPadEvent(int _device_index, int _x, int _y, Kind _kind, int _velocity, LaunchpadProtocol::Model _model)
    : device_index(_device_index), x(_x), y(_y), kind(_kind), velocity(_velocity), model(_model) { }

  void dispatch(EventHandler & evh) override { evh.handleLaunchpadPadEvent(*this); }

  int getDeviceIndex() const { return device_index; }
  int getX() const { return x; }
  int getY() const { return y; }
  Kind getKind() const { return kind; }
  int getVelocity() const { return velocity; }
  LaunchpadProtocol::Model getModel() const { return model; }

 private:
  int device_index, x, y;
  Kind kind;
  int velocity;
  LaunchpadProtocol::Model model;
};

#endif
