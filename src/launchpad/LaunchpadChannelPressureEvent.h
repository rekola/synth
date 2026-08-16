#ifndef _LAUNCHPADCHANNELPRESSUREEVENT_H_
#define _LAUNCHPADCHANNELPRESSUREEVENT_H_

#include "../playback/Event.h"
#include "../playback/EventHandler.h"
#include "LaunchpadProtocol.h"

// A device-wide (not per-pad) channel-pressure message - the alternative
// mode a grid controller can be configured to send aftertouch in instead
// of LaunchpadPadEvent::AFTERTOUCH's per-pad polyphonic pressure (the two
// are mutually exclusive on real hardware, never both at once). Mirrors
// LaunchpadButtonEvent's shape (no x/y, since this isn't tied to any one
// pad).
class LaunchpadChannelPressureEvent : public Event {
 public:
  LaunchpadChannelPressureEvent(int _device_index, int _velocity, LaunchpadProtocol::Model _model)
    : device_index(_device_index), velocity(_velocity), model(_model) { }

  void dispatch(EventHandler & evh) override { evh.handleLaunchpadChannelPressureEvent(*this); }

  int getDeviceIndex() const { return device_index; }
  int getVelocity() const { return velocity; }
  LaunchpadProtocol::Model getModel() const { return model; }

 private:
  int device_index, velocity;
  LaunchpadProtocol::Model model;
};

#endif
