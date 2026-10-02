#include "TestFramework.h"

#include "../src/instruments/NoteMultiplier.h"
#include "../src/model/Track.h"
#include "../src/model/NoteCoordinate.h"
#include "../src/state/MemoryParameterSource.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ambisonic/SphericalPosition.h"

#include <cmath>
#include <vector>

using namespace std;

namespace {

// Records every detune ratio it's asked to play, and otherwise produces no
// real voice (nullptr) - just enough of a leaf Track for NoteMultiplier's
// own fan-out to exercise.
class RecordingLeaf : public Track {
 public:
  RecordingLeaf() : Track(TrackType::INSTRUMENT) { }
  const char * getElementName() const override { return "recording-leaf"; }

  mutable vector<float> detunes;

  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration &, const SphericalPosition &, Tuning, float detune, float, int, const SendLevels &, const NoteCoordinate &, bool) const override {
    detunes.push_back(detune);
    return nullptr;
  }
};

unique_ptr<NoteMultiplier> makeMultiplier(int octaves, float detune_cents) {
  auto m = make_unique<NoteMultiplier>();
  MemoryParameterSource params;
  // unisons=0 (neither the plain unisons_==1 passthrough nor the
  // unisons_>=2 spread branch) isolates the octaves_
  // loop below, which run unconditionally alongside whichever of those
  // two the real "Dual Strings"-style usage (unisons left at its default
  // of 1) would also produce.
  params.set("unisons", 0);
  params.set("octaves", octaves);
  params.set("detune", detune_cents);
  m->loadParameters(params);
  return m;
}

} // namespace

// octaves_'s own per-voice random jitter must be a small
// cents-scale perturbation around the exact interval, matching the
// unisons_ >= 2 branch's own cents convention (detune_/1200) - not the raw
// fractional multiplier the formula used before, which let a modest
// detune="6" swing a nominally-one-octave-up voice as far as three octaves
// away.
TEST(octaves_detune_jitter_stays_within_cents_of_the_exact_octave) {
  auto multiplier = makeMultiplier(/*octaves*/ 1, /*detune_cents*/ 6.0f);
  auto leaf_owner = make_unique<RecordingLeaf>();
  RecordingLeaf & leaf = *leaf_owner;
  multiplier->addChild(std::move(leaf_owner));

  ChannelConfiguration config(44100, 1);
  SphericalPosition position;

  // Several different note coordinates to sample the HashField draw at
  // different points, not just one fixed jitter value.
  for (int row = 0; row < 20; row++) {
    NoteCoordinate coord(0, row * 8, 0);
    multiplier->playNote(config, position, Tuning::TET31, 1.0f, 1.0f, 60, SendLevels{}, coord, false);
  }

  CHECK(leaf.detunes.size() == 20);
  float max_cents_bound = powf(2.0f, 6.0f / 1200.0f); // detune="6" -> +/-6 cents
  float min_cents_bound = powf(2.0f, -6.0f / 1200.0f);
  bool saw_any_spread = false;
  for (float d : leaf.detunes) {
    float ratio_to_exact_octave = d / 2.0f;
    CHECK(ratio_to_exact_octave <= max_cents_bound);
    CHECK(ratio_to_exact_octave >= min_cents_bound);
    if (fabsf(ratio_to_exact_octave - 1.0f) > 1e-6f) saw_any_spread = true;
  }
  CHECK(saw_any_spread); // still genuinely jittered, not silently clamped to 0
}
