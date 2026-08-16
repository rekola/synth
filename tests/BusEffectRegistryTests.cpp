#include "TestFramework.h"

#include "../src/bus/BusEffectRegistry.h"
#include "../src/Song.h"
#include "../src/InstrumentProvider.h"
#include "../src/ChannelConfiguration.h"
#include "../src/OfflineRenderer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif
#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

namespace {

struct Loaded {
  bool ok;
  Song song;
};

Loaded loadFixture(const char * name) {
  InstrumentProvider provider; // no SoundFont: fixtures only use built-in oscillators
  Song song;
  bool ok = song.open(std::string(TESTS_FIXTURES_DIR) + "/" + name, provider);
  return { ok, std::move(song) };
}

float windowedRms(const OfflineRenderResult & result, int channel, float start_s, float end_s) {
  auto frames = result.numberOfFrames();
  size_t start = std::min<size_t>(static_cast<size_t>(start_s * result.sampleRate), frames);
  size_t end = std::min<size_t>(static_cast<size_t>(end_s * result.sampleRate), frames);
  if (end <= start) return 0.0f;
  double sum = 0.0;
  for (size_t i = start; i < end; i++) sum += std::pow(result.interleaved[i * static_cast<size_t>(result.channels) + static_cast<size_t>(channel)], 2);
  return static_cast<float>(std::sqrt(sum / (end - start)));
}

}

TEST(bus_effect_registry_resolves_known_names_and_rejects_unknown) {
  auto * reverb = findBusEffectDescriptor("reverb");
  auto * delay = findBusEffectDescriptor("delay");
  auto * none = findBusEffectDescriptor("none");
  CHECK(reverb && reverb->kind == BusEffectKind::Reverb);
  CHECK(delay && delay->kind == BusEffectKind::Delay);
  CHECK(none && none->kind == BusEffectKind::None);
  CHECK(findBusEffectDescriptor("not_a_real_effect_type") == nullptr);

  CHECK(findBusEffectDescriptor(BusEffectKind::Reverb).kind == BusEffectKind::Reverb);
  CHECK(findBusEffectDescriptor(BusEffectKind::Delay).kind == BusEffectKind::Delay);
  CHECK(findBusEffectDescriptor(BusEffectKind::None).kind == BusEffectKind::None);
}

TEST(bus_effect_registry_resolves_haze) {
  auto * haze = findBusEffectDescriptor("haze");
  CHECK(haze && haze->kind == BusEffectKind::Haze);
  CHECK(findBusEffectDescriptor(BusEffectKind::Haze).kind == BusEffectKind::Haze);
}

TEST(song_default_bus_has_reverb_in_a_and_delay_in_b) {
  Song song;
  CHECK(song.getBusSlotKind(0) == BusEffectKind::Reverb);
  CHECK(song.getBusSlotKind(1) == BusEffectKind::Delay);
}

TEST(song_bus_element_resolves_slots_by_document_order) {
  // bus_swapped_occupancy.xml's <bus> lists <delay/> then <reverb/> -
  // slot 0 (A) must resolve to Delay and slot 1 (B) to Reverb, the
  // reverse of the compiled-in default.
  auto loaded = loadFixture("bus_swapped_occupancy.xml");
  CHECK(loaded.ok);
  CHECK(loaded.song.getBusSlotKind(0) == BusEffectKind::Delay);
  CHECK(loaded.song.getBusSlotKind(1) == BusEffectKind::Reverb);
}

TEST(song_bus_unknown_element_falls_back_to_slot_default_type) {
  // bus_unknown_slot_b_type.xml's second <bus> child names a type this
  // build doesn't know - slot 1 (B) must fall back to its own default
  // (Delay), not None and not a crash.
  auto loaded = loadFixture("bus_unknown_slot_b_type.xml");
  CHECK(loaded.ok);
  CHECK(loaded.song.getBusSlotKind(0) == BusEffectKind::Reverb);
  CHECK(loaded.song.getBusSlotKind(1) == BusEffectKind::Delay);
}

TEST(song_default_bus_round_trips_through_save_without_writing_bus_element) {
  namespace fs = std::filesystem;
  auto scratch_path = fs::path(TESTS_SCRATCH_DIR) / "bus_default_round_trip_scratch.xml";

  Song song;
  song.save(scratch_path.string());

  std::ifstream in(scratch_path.string());
  std::ostringstream ss;
  ss << in.rdbuf();
  CHECK(ss.str().find("<bus") == std::string::npos);

  InstrumentProvider provider;
  Song reloaded_song;
  CHECK(reloaded_song.open(scratch_path.string(), provider));
  CHECK(reloaded_song.getBusSlotKind(0) == BusEffectKind::Reverb);
  CHECK(reloaded_song.getBusSlotKind(1) == BusEffectKind::Delay);
}

TEST(render_bus_chain_send_routes_delay_into_reverb) {
  // Both fixtures feed the same note through sendB="0.5" into a delay
  // with feedback disabled (so its own taps decay away within ~0.2s) and
  // a reverb with a long (3s) decay in slot A - the only difference is
  // slot B's chainSend (1.0 vs 0.0). A late window, well past the
  // delay's own taps but well within the reverb's tail, should therefore
  // be audible only in the chainSend=1.0 case - directly exercising the
  // slot B -> slot A chain-send path (bus/SendBusProcessor.cpp), not just
  // each effect's own isolated math.
  auto with_chain = loadFixture("bus_chain_send_on.xml");
  auto without_chain = loadFixture("bus_chain_send_off.xml");
  CHECK(with_chain.ok);
  CHECK(without_chain.ok);

  ChannelConfiguration config(44100, 1);
  auto result_with = renderSongOffline(with_chain.song, config);
  auto result_without = renderSongOffline(without_chain.song, config);

  auto tailRms = [&](const OfflineRenderResult & result) {
    return windowedRms(result, 0, 1.0f, 1.5f) + windowedRms(result, 1, 1.0f, 1.5f);
  };

  auto with_tail = tailRms(result_with);
  auto without_tail = tailRms(result_without);

  CHECK(with_tail > 1e-5f);
  CHECK(with_tail > without_tail * 5.0f);
}
