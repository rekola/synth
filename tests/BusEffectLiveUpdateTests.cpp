#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/state/SongState.h"
#include "../src/bus/BusEffectRegistry.h"
#include "../src/bus/FDNReverb.h"
#include "../src/bus/MultiTapDelay.h"
#include "../src/bus/GranularCloud.h"
#include "../src/bus/Haze.h"
#include "../src/ambisonic/ChannelConfiguration.h"

using namespace std;

// SongState::setBusEffectKind() (Controller::setBusEffectKind()'s own
// target, via Player.cpp's SET_BUS_EFFECT handling - see UI.cpp's
// set-bus-effect-a/-b commands) is the live-reconfiguration sibling of
// initialize()'s load-time-only per-slot construction: it must swap the
// concrete BusEffect a slot's already-running SendBusProcessor holds,
// without needing a fresh SongState/initialize() call.
TEST(song_state_initializes_default_bus_slots) {
  Song song; // compiled default: slot A = reverb, slot B = delay
  ChannelConfiguration config(44100, 1);
  SongState state(config);
  state.initialize(song);

  CHECK(dynamic_cast<const FDNReverb *>(&state.getSlotEffectForTest(0)) != nullptr);
  CHECK(dynamic_cast<const MultiTapDelay *>(&state.getSlotEffectForTest(1)) != nullptr);
}

TEST(set_bus_effect_kind_swaps_the_live_slot_without_reinitializing) {
  Song song;
  ChannelConfiguration config(44100, 1);
  SongState state(config);
  state.initialize(song);
  CHECK(dynamic_cast<const FDNReverb *>(&state.getSlotEffectForTest(0)) != nullptr);

  state.setBusEffectKind(0, BusEffectKind::Granular);
  CHECK(dynamic_cast<const GranularCloud *>(&state.getSlotEffectForTest(0)) != nullptr);
  CHECK(dynamic_cast<const FDNReverb *>(&state.getSlotEffectForTest(0)) == nullptr);

  state.setBusEffectKind(1, BusEffectKind::Haze);
  CHECK(dynamic_cast<const Haze *>(&state.getSlotEffectForTest(1)) != nullptr);
  CHECK(dynamic_cast<const MultiTapDelay *>(&state.getSlotEffectForTest(1)) == nullptr);

  state.setBusEffectKind(0, BusEffectKind::None);
  CHECK(state.getSlotEffectForTest(0).getNumTaps() == 0);
  CHECK(dynamic_cast<const NullBusEffect *>(&state.getSlotEffectForTest(0)) != nullptr);
}

// Song::setBusSlotKind() (Controller::setBusEffectKind()'s own model-side
// half) is independent of the live SongState above - editing the model
// must not, by itself, reach an already-initialize()'d SongState's own
// send_bus_ slot (that's exactly what the separate setBusEffectKind() call
// above is for).
TEST(song_set_bus_slot_kind_does_not_reach_an_already_initialized_songstate) {
  Song song;
  ChannelConfiguration config(44100, 1);
  SongState state(config);
  state.initialize(song);
  CHECK(dynamic_cast<const FDNReverb *>(&state.getSlotEffectForTest(0)) != nullptr);

  song.setBusSlotKind(0, BusEffectKind::Delay);
  CHECK(dynamic_cast<const FDNReverb *>(&state.getSlotEffectForTest(0)) != nullptr);
}
