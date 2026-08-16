#ifndef _CHANNELCONFIGURATION_H_
#define _CHANNELCONFIGURATION_H_

#include "../util/constants.h"

class ChannelConfiguration {
 public:
  // ambisonic_order 0 = mono (no directional content - a single
  // omnidirectional/W channel, conceptually 0th-order ambisonics), 1 =
  // first-order/FOA (4 channels), 2 = second-order (9 channels), 3 =
  // third-order (16 channels) - see AmbisonicEncoding.h's kAmbisonicOrder
  // hard ceiling.
  explicit ChannelConfiguration(int audioOutSampleRate = 44100, int ambisonic_order = 0)
    : audioOutSampleRate_(audioOutSampleRate), ambisonic_order_(ambisonic_order) { }

  bool operator==(const ChannelConfiguration & other) const {
    return audioOutSampleRate_ == other.audioOutSampleRate_ && ambisonic_order_ == other.ambisonic_order_
      && ear_height_ == other.ear_height_ && floor_reflection_enabled_ == other.floor_reflection_enabled_
      && floor_reflection_strength_ == other.floor_reflection_strength_ && ground_absorption_ == other.ground_absorption_;
  }

  bool isMono() const { return ambisonic_order_ == 0; }
  bool isAmbisonic() const { return ambisonic_order_ > 0; }

  int numberOfChannels() const { return (ambisonic_order_ + 1) * (ambisonic_order_ + 1); }

  // Every mixer ultimately produces a 2-channel stereo device signal - a
  // MONO (0th-order-ambisonic, W-only) bus broadcasts equally to both
  // channels via decodeToStereo()'s 1-channel case rather than being a
  // genuine 1-channel device output; an AMBISONIC bus decodes via binaural
  // or cardioid. There is no other device-output shape.
  int getDeviceChannels() const { return 2; }

  int getAudioOutSampleRate() const { return audioOutSampleRate_; }
  int getAmbisonicOrder() const { return ambisonic_order_; }

  void setAudioOutSampleRate(int audioOutSampleRate) { audioOutSampleRate_ = audioOutSampleRate; }
  void setAmbisonicOrder(int ambisonic_order) { ambisonic_order_ = ambisonic_order; }

  inline float getRowDuration(int tempo) const {
    return 60.0f / 4.0f / tempo;
  }

  inline int getSampleInterval(int tempo) const {
    return static_cast<int>(getRowDuration(tempo) * getAudioOutSampleRate());
  }

  // Song-level floor-reflection parameters (see InstrumentVoice.h) -
  // fixed for a song's whole lifetime, same as audioOutSampleRate_/
  // ambisonic_order_ above, and threaded the same way: every playNote()
  // call already carries a ChannelConfiguration all the way down to
  // voice construction, so this is the one place a song-wide constant
  // can reach a voice's constructor without a new parameter threaded
  // through every Track subclass's playNote() override. Defaulted here
  // (not left at 0) so every existing two-argument ChannelConfiguration
  // construction site - tests, offline tools - keeps getting sensible
  // values without having to know about this feature at all;
  // SongState::initialize() overwrites them from the real Song once a
  // song is actually loaded (mirroring setAudioOutSampleRate()/
  // setAmbisonicOrder()'s own existing call pattern in main.cpp).
  float getEarHeight() const { return ear_height_; }
  bool getFloorReflectionEnabled() const { return floor_reflection_enabled_; }
  float getFloorReflectionStrength() const { return floor_reflection_strength_; }
  float getGroundAbsorption() const { return ground_absorption_; }

  void setEarHeight(float h) { ear_height_ = h; }
  void setFloorReflectionEnabled(bool e) { floor_reflection_enabled_ = e; }
  void setFloorReflectionStrength(float s) { floor_reflection_strength_ = s; }
  void setGroundAbsorption(float a) { ground_absorption_ = a; }

 private:
  int audioOutSampleRate_;
  int ambisonic_order_;
  float ear_height_ = constants::DEFAULT_EAR_HEIGHT;
  bool floor_reflection_enabled_ = constants::DEFAULT_FLOOR_REFLECTION_ENABLED;
  float floor_reflection_strength_ = constants::DEFAULT_FLOOR_REFLECTION_STRENGTH;
  float ground_absorption_ = constants::DEFAULT_GROUND_ABSORPTION;
};

#endif
