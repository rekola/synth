#include "BusEffectRegistry.h"
#include "FDNReverb.h"
#include "MultiTapDelay.h"
#include "GranularCloud.h"
#include "Haze.h"

#include <cassert>

using namespace std;

namespace {
unique_ptr<BusEffect> makeNone(int sampleRate) { return make_unique<NullBusEffect>(sampleRate); }
unique_ptr<BusEffect> makeReverb(int sampleRate) { return make_unique<FDNReverb>(sampleRate); }
unique_ptr<BusEffect> makeDelay(int sampleRate) { return make_unique<MultiTapDelay>(sampleRate); }
unique_ptr<BusEffect> makeGranular(int sampleRate) { return make_unique<GranularCloud>(sampleRate); }
unique_ptr<BusEffect> makeHaze(int sampleRate) { return make_unique<Haze>(sampleRate); }
}

const std::array<BusEffectDescriptor, 5> &
busEffectRegistry() {
  static const std::array<BusEffectDescriptor, 5> registry{{
    { BusEffectKind::None,     "none",     &makeNone },
    { BusEffectKind::Reverb,   "reverb",   &makeReverb },
    { BusEffectKind::Delay,    "delay",    &makeDelay },
    { BusEffectKind::Granular, "granular", &makeGranular },
    { BusEffectKind::Haze,     "haze",     &makeHaze },
  }};
  return registry;
}

const BusEffectDescriptor *
findBusEffectDescriptor(const std::string & xmlName) {
  for (auto & entry : busEffectRegistry()) {
    if (xmlName == entry.xmlName) return &entry;
  }
  return nullptr;
}

const BusEffectDescriptor &
findBusEffectDescriptor(BusEffectKind kind) {
  for (auto & entry : busEffectRegistry()) {
    if (entry.kind == kind) return entry;
  }
  assert(0); // every BusEffectKind has exactly one registry entry
  return busEffectRegistry()[0];
}
