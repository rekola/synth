#ifndef _STATEFULSONGOBJECT_H_
#define _STATEFULSONGOBJECT_H_

#include "SongObject.h"
#include "TrackState.h"

class StatefulSongObject : public SongObject {
 public:
  StatefulSongObject() { }

  virtual std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const = 0;

  void loadParameters(const ParameterSource & input) override {
    SongObject::loadParameters(input);
    setVolume(input.getFloat("volume", 1.0f));
  }

  void storeParameters(ParameterSource & output) const override {
    SongObject::storeParameters(output);
    if (getVolume() != 1.0f) output.set("volume", getVolume());
  }

  float getVolume() const { return volume_; }
  void setVolume(float volume) { volume_ = volume; }

private:
  float volume_ = 1.00f;
};

#endif
