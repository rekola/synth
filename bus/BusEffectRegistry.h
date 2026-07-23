#ifndef _BUSEFFECTREGISTRY_H_
#define _BUSEFFECTREGISTRY_H_

#include "BusEffect.h"

#include <array>
#include <memory>
#include <string>

// A real, always-non-null occupant for "no effect in this slot" (see
// Song.cpp's <bus> loading) - process() no-ops, getNumTaps() returns 0.
// Exists so SendBusProcessor's per-slot loop never needs a null check: an
// empty slot is just an effect with zero taps, which the uniform per-tap
// encode loop (and BusEffect::getChainSendSum()'s default sum-of-taps)
// already handle as a trivial no-op/zero result. Uses SongObject's
// default loadParameters()/storeParameters() (id/name only) unchanged -
// "None has none" own parameters to (de)serialize.
class NullBusEffect : public BusEffect {
 public:
  explicit NullBusEffect(int sampleRate) : BusEffect(sampleRate) { }
  void process(const float *, int) override { }
  int getNumTaps() const override { return 0; }
  const float * getTap(int) const override { return nullptr; } // never called: getNumTaps() == 0
  SphericalPosition getTapDirection(int) const override { return SphericalPosition{}; } // never called
};

// Fixed compile-time set of slot-effect types, chosen by name - the same
// "chosen by name, not reflection" idiom Song.cpp's createTrack() already
// uses for per-track effects/instruments (extensible by code, not by
// users). `None` is the empty-slot occupant above; the rest are the real
// bus effects (just Reverb/Delay so far - Granular is a later addition).
enum class BusEffectKind { None, Reverb, Delay };

struct BusEffectDescriptor {
  BusEffectKind kind;
  const char * xmlName;              // "none" / "reverb" / "delay"
  std::unique_ptr<BusEffect> (*factory)(int sampleRate);
  // No load/save function pointers here - each concrete BusEffect
  // subclass implements its own loadParameters()/storeParameters()
  // (SongObject.h), invoked polymorphically once a slot's effect instance
  // has been constructed (see Song.cpp's <bus> loading). The registry's
  // only job is picking *which* concrete type to construct from an XML
  // element name; what that type does with its own attributes from there
  // is entirely its own business.
};

const std::array<BusEffectDescriptor, 3> & busEffectRegistry();

// Looks up a registry entry by its XML element name; returns nullptr if
// not found - an unrecognized element name, which callers (Song.cpp) fall
// back on that slot's own default type for, per the project-file plan.
const BusEffectDescriptor * findBusEffectDescriptor(const std::string & xmlName);

// Looks up a registry entry by kind - unlike the xmlName overload above,
// this never fails (every BusEffectKind value has exactly one registry
// entry, by construction), so it returns a reference, not a possibly-null
// pointer.
const BusEffectDescriptor & findBusEffectDescriptor(BusEffectKind kind);

#endif
