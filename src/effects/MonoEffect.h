#ifndef _MONOEFFECT_H_
#define _MONOEFFECT_H_

#include "Effect.h"
#include "../ambisonic/AmbisonicEncoding.h"
#include "../ambisonic/SphericalPosition.h"

// Shared base for the per-track effects whose own DSP genuinely needs
// single-channel (or, for Chorus, a decorrelated near-mono pair) input
// rather than the real ambisonic signal - Chorus/Distortion/
// TapeDegradation, via the identical getChildChannelConfiguration()
// override below (every one of them already had this, verbatim, before
// this class existed). Not about being "spatial" - a linear per-channel
// filter (BiquadFilter/ResonantFilter) commutes correctly with ambisonic
// encoding and stays outside this hierarchy entirely, still touching the
// real channels directly; it's Distortion's own nonlinearity (see its
// class comment) or Chorus's need to manufacture width from a point
// source that forces a reduce-then-reencode round trip. That round trip
// needs a real spatial position to reencode into, which is what this class
// actually holds: Track-attached, its own authored azimuth_/elevation_/
// distance_/extent_ below; voice-attached, whatever playNote() was
// actually given (each subclass's own playNote() override captures it -
// see TapeDegradation.cpp for the canonical shape).
class MonoEffect : public Effect {
 public:
  void loadParameters(const ParameterSource & input) override {
    Effect::loadParameters(input);
    azimuth_ = input.getFloat("azimuth");
    distance_ = input.getFloat("distance");
    elevation_ = input.getFloat("elevation");
    extent_ = input.getFloat("extent", -1.0f);
  }

  void storeParameters(ParameterSource & output) const override {
    Effect::storeParameters(output);
    output.set("azimuth", azimuth_);
    output.set("distance", distance_);
    output.set("elevation", elevation_);
    output.set("extent", extent_, -1.0f);
  }

  void setElevation(float e) { elevation_ = e; }
  void setAzimuth(float a) { azimuth_ = a; }
  void setDistance(float d) { distance_ = d; }
  void setExtent(float e) { extent_ = e; }

  float getElevation() const { return elevation_; }
  float getAzimuth() const { return azimuth_; }
  float getDistance() const { return distance_; }
  float getExtent() const { return extent_; }

  SphericalPosition getPosition() const { return { azimuth_, elevation_, distance_, extent_ }; }

  ChannelConfiguration getChildChannelConfiguration(const ChannelConfiguration & config) const override { return reduceForEffect(config); }

 private:
  float azimuth_ = 0.0f, elevation_ = 0.0f, distance_ = 0.0f, extent_ = -1.0f;
};

#endif
