#include "LaunchpadLayout.h"

#include <algorithm>
#include <cmath>

using namespace std;

namespace LaunchpadLayout {

namespace {
  // Starting points for the hue tree - tunable, not load-bearing for pitch
  // classification (only the LED-coloring result). TONIC and FOURTH/FIFTH
  // are fixed, prominent pop-out hues (yellow-leaning-red, amber-leaning-
  // red); RECURSIVE stays in a separate blue/violet band (kRecursiveBaseHue
  // below). Both choices are confirmed against real Launchpad X hardware:
  // green LEDs read as much brighter/more prominent than blue or purple
  // ones regardless of the saturation value sent, so every hue here is kept
  // clear of green (~90-160) - including *the short way around the wheel*,
  // where a hue near 360/0 can sit close to a low hue value like
  // kFourthHue/kFifthHue even though a naive linear reading of the two
  // numbers looks far apart.
  // FOURTH and FIFTH get their own close-but-distinct hues (a small split
  // around the same amber center, same spirit as kDepth3HueOffset below) -
  // a single shared hue made the two tiers indistinguishable at a glance,
  // so each gets its own, even though they're close.
  constexpr float kTonicHue = 50.0f; // yellow, nudged toward red
  constexpr float kFourthFifthCenterHue = 18.0f; // amber, nudged further toward red
  constexpr float kFourthFifthHueOffset = 6.0f;
  constexpr float kFourthHue = kFourthFifthCenterHue - kFourthFifthHueOffset;
  constexpr float kFifthHue = kFourthFifthCenterHue + kFourthFifthHueOffset;
  // Center of the RECURSIVE-tier hue drift - nudged toward true blue (240)
  // rather than sitting exactly between blue and magenta, so the
  // minor-family side reads as recognizably blue rather than violet.
  constexpr float kRecursiveBaseHue = 260.0f;
  // Only depth 3 and depth 4+ ever reach the device as distinct hues
  // (every depth from 4 on shares kDepth4HueOffset) - fixed, deliberately
  // asymmetric offsets rather than a geometric decay. kDepth3HueOffset is
  // small so major/minor at the *same* depth (e.g. E vs. Eb, both depth 3)
  // read as close/related; kDepth4HueOffset is larger so depth 3 vs.
  // depth 4+ within the *same* family (e.g. A# vs. G-double-sharp, both
  // major) read as clearly different depths - but not so large that the
  // major-family depth-4+ hue (kRecursiveBaseHue + kDepth4HueOffset)
  // crosses into FOURTH/FIFTH's amber the short way around the wheel (310
  // is ~62 degrees from kFourthHue, not the ~280 a naive linear reading of
  // "310 vs ~20" suggests). kDepth3HueOffset stays well above 0, since
  // collapsing it away would erase the major/minor distinction at depth 3
  // altogether, not just make it subtle.
  constexpr float kDepth3HueOffset = 8.0f;
  constexpr float kDepth4HueOffset = 50.0f;

  // One enharmonic-collision finding from collectMediantCollisions() below:
  // pitch class `step` is reachable via two distinct, independently-real
  // JI ratios that this EDO can't tell apart, first discovered `depth`
  // mediant-splits in (see that function's own comment - shallower means
  // a more fundamental, "louder" ambiguity).
  struct MediantCollision { int step, depth; };

  // Recursively walks the mediant-splitting tree of superparticular JI
  // ratios (p:q with p-q==1) rooted at the octave (2:1) - the exact same
  // construction computeConsonanceLevels() already uses once, to turn the
  // fifth (3:2) into minor_third*major_third (6:5*5:4): doubling a
  // ratio's numerator and denominator (2p:2q) and inserting their
  // arithmetic mean (p+q) between them splits it into two more
  // superparticular ratios, 2p:(p+q) and (p+q):2q, whose product
  // recomposes the original. Run one level further, this reaches
  // minor_third's own children, 12:11 and 11:10 - both independently real
  // ("undecimal neutral second") ratios in their own right, not just an
  // arithmetic byproduct, which is what makes it a genuine finding rather
  // than noise when the two of them land on the same EDO step (31-EDO:
  // "C-double-sharp", the case this function was written to catch).
  // Every pitch class this reaches is measured from the tonic, so the
  // recursion only ever explores the octave's lower half (ratios
  // shrinking toward 1:1/unison as p,q grow) - the caller mirrors each
  // finding across the octave's midpoint to cover the upper half
  // (sixths, sevenths, ...) instead of exploring a second tree.
  // Self-terminating and EDO-adaptive rather than a fixed depth or a
  // fixed list of named ratio pairs to check: recursion stops once a
  // ratio's own span is already narrower than a single EDO step, since
  // past that point every further split is guaranteed to collide
  // trivially - a coarser EDO simply runs out of resolution sooner than a
  // finer one does, without needing to know in advance how deep to look.
  // `depth` is the depth of `p:q` itself (root octave = 0); a collision
  // found between its two children is recorded at depth+1.
  void collectMediantCollisions(int p, int q, int depth, int edo_steps, vector<MediantCollision> & collisions) {
    auto cents_steps = edo_steps * log2(static_cast<double>(p) / q);
    if (cents_steps < 1.0) return; // no resolvable structure left below this
    auto mediant = p + q;
    auto step_of = [edo_steps](int a, int b) {
      auto raw = static_cast<int>(lround(edo_steps * log2(static_cast<double>(a) / b)));
      return ((raw % edo_steps) + edo_steps) % edo_steps;
    };
    auto child_depth = depth + 1;
    auto lower_step = step_of(2 * p, mediant);
    auto upper_step = step_of(mediant, 2 * q);
    if (lower_step == upper_step) collisions.push_back({lower_step, child_depth});
    collectMediantCollisions(2 * p, mediant, child_depth, edo_steps, collisions);
    collectMediantCollisions(mediant, 2 * q, child_depth, edo_steps, collisions);
  }
}

int edoSteps(Tuning tuning) {
  switch (tuning) {
  case Tuning::TET12: return 12;
  case Tuning::TET19: return 19;
  case Tuning::TET31: return 31;
  case Tuning::TET53: return 53;
  case Tuning::PERCUSSION: return 0; // no fixed pitch structure
  }
  return 0;
}

Basis
computeBasis(int edo_steps) {
  Basis basis;
  basis.fifth = static_cast<int>(lround(edo_steps * log2(3.0 / 2.0)));
  basis.whole_tone = 2 * basis.fifth - edo_steps;
  basis.semitone = 3 * edo_steps - 5 * basis.fifth;
  basis.major_third = static_cast<int>(lround(edo_steps * log2(5.0 / 4.0)));
  basis.minor_third = static_cast<int>(lround(edo_steps * log2(6.0 / 5.0)));
  basis.degenerate = basis.whole_tone <= 0 || basis.semitone <= 0 || basis.whole_tone == basis.semitone;
  return basis;
}

int
noteForPad(const Basis & basis, int x, int y, int base_note) {
  if (basis.degenerate) return base_note + x + y * 8;
  return base_note + x * basis.whole_tone + y * basis.semitone;
}

vector<PadClassification>
computeConsonanceLevels(const Basis & basis, int edo_steps) {
  vector<PadClassification> result(static_cast<size_t>(edo_steps));
  vector<bool> covered(static_cast<size_t>(edo_steps), false);

  // First assignment for a pitch class wins - see the tie-breaking note in
  // this function's header doc comment (a coarse EDO can reach the same
  // pitch class via more than one span at the same depth).
  auto mark = [&](int pitch, PadTier tier, float hue, int depth) {
    auto p = ((pitch % edo_steps) + edo_steps) % edo_steps;
    if (covered[static_cast<size_t>(p)]) return;
    covered[static_cast<size_t>(p)] = true;
    result[static_cast<size_t>(p)] = {tier, hue, depth};
  };

  mark(0, PadTier::TONIC, kTonicHue, 1);

  // Level 2: the octave factors via the fifth, the simplest non-trivial
  // consonance - 2/1 = 4/3 * 3/2.
  auto P5 = basis.fifth;
  auto P4 = edo_steps - P5;
  mark(P4, PadTier::FOURTH, kFourthHue, 2);
  mark(P5, PadTier::FIFTH, kFifthHue, 2);

  // Level 3: every level-2 span factors the way the fifth itself does -
  // 3/2 = 6/5 * 5/4 - rescaled to the span's own length (exact for a
  // fifth-length span, since major_third+minor_third == fifth for every
  // EDO this engine supports; an approximation of "this span's own
  // simplest factor pair" for a fourth-length one). This is also where a
  // landmark's family (major/minor) is decided - level 4+ below just
  // inherits it. Fifth-descended spans are split before fourth-descended
  // ones so ties resolve consistently. The hue drift starts from
  // kRecursiveBaseHue, deliberately decoupled from FOURTH/FIFTH's own
  // (unrelated, fixed) amber hue - see this file's own top-of-namespace
  // comment.
  struct Span { int a, b; float hue; bool is_major; };
  vector<Span> spans;
  {
    for (auto [a, b] : {pair{0, P5}, pair{P5, edo_steps}, pair{0, P4}, pair{P4, edo_steps}}) {
      auto length = b - a;
      auto larger = static_cast<int>(lround(length * static_cast<double>(basis.major_third) / basis.fifth));
      auto smaller = length - larger;
      if (larger <= 0 || smaller <= 0) continue; // degenerate for this span's size

      auto major_landmark = a + larger;
      auto minor_landmark = a + smaller;
      auto major_hue = kRecursiveBaseHue + kDepth3HueOffset;
      auto minor_hue = kRecursiveBaseHue - kDepth3HueOffset;

      mark(major_landmark, PadTier::RECURSIVE, major_hue, 3);
      mark(minor_landmark, PadTier::RECURSIVE, minor_hue, 3);

      spans.push_back({a, major_landmark, major_hue, true});
      spans.push_back({major_landmark, b, major_hue, true});
      spans.push_back({a, minor_landmark, minor_hue, false});
      spans.push_back({minor_landmark, b, minor_hue, false});
    }
  }

  // Level 4+: unlike level 3 (which needs the fifth's own major/minor
  // ratio to have a musical basis at all), there's no further hand-named
  // next-generation ratio to rescale - so each span is just bisected by
  // step count, producing one new landmark that inherits its parent
  // span's family (major/minor) rather than a fresh decision. Confirmed
  // against a worked example (31-EDO, key of C): D and C-double-sharp
  // both land at depth 4 this way (from bisecting the C-E and C-Eb spans
  // respectively) - the earlier version of this function (using the same
  // rescaled-fifth-ratio formula at every depth, not just level 3) put
  // them at different depths (4 vs 5) purely from an arithmetic
  // coincidence (bisecting a length-8 span under that formula collapses
  // to a single repeated position), which read as an arbitrary
  // inconsistency between two landmarks a listener would expect to be
  // peers.
  for (int depth = 4; !spans.empty(); depth++) {
    // Only depth 4 itself is ever actually displayed as a distinct hue
    // (see consonanceColor()) - depth 5+ reuses kDepth4HueOffset too since
    // its hue value is never read, only its position/depth/family.
    auto offset = kDepth4HueOffset;
    vector<Span> next_spans;
    for (auto & span : spans) {
      auto length = span.b - span.a;
      if (length <= 1) continue; // nothing left to split

      auto mid = span.a + static_cast<int>(lround(length / 2.0));
      if (mid == span.a || mid == span.b) continue; // degenerate for this span's size

      auto hue = kRecursiveBaseHue + (span.is_major ? offset : -offset);
      mark(mid, PadTier::RECURSIVE, hue, depth);

      next_spans.push_back({span.a, mid, hue, span.is_major});
      next_spans.push_back({mid, span.b, hue, span.is_major});
    }
    spans = move(next_spans);
  }

  // Enharmonic-collision overlay: the recursion above always picks a
  // family for every pitch class, even where the EDO is too coarse to
  // keep two distinct, independently-real JI ratios apart (e.g. 9/8 and
  // 10/9 both rounding to the same step - "D" - in 12/19/31-EDO) - it
  // just silently keeps whichever family's landmark got there first. Fill
  // in enharmonic_blend for those pitch classes instead, so the coloring
  // can show *how much* it isn't cleanly one family rather than silently
  // keeping a false precision the tuning doesn't have.
  // collectMediantCollisions() only explores the octave's lower half (see
  // its own comment), so each finding is mirrored across the octave's
  // midpoint to cover its upper-half counterpart too. A pitch class is
  // often found more than once, at various depths (a coarse EDO's near-
  // tonic region in particular - every branch of the mediant tree shrinks
  // toward unison, so many independent, ever-finer JI ratio pairs all
  // eventually collide there too). Every hit adds to that pitch class's
  // score, weighted by depth: each level in costs half, mirroring how a
  // mediant split roughly halves the remaining interval - so a handful of
  // shallow (simple-ratio) collisions and a great many deep (complex-
  // ratio) ones can land on comparable scores, both being real evidence
  // the tuning is straining there. Scores are then normalized against the
  // single highest-scoring pitch class in this EDO (always the near-
  // unison region, where the mediant tree's many branches pile up densest
  // - the tuning's own single worst spot), so enharmonic_blend reads as
  // "how bad is this, relative to the worst case this EDO actually has"
  // rather than on some fixed, cross-EDO absolute scale.
  vector<MediantCollision> collisions;
  collectMediantCollisions(2, 1, 0, edo_steps, collisions);
  vector<float> score(static_cast<size_t>(edo_steps), 0.0f);
  vector<int> min_depth(static_cast<size_t>(edo_steps), 0); // 0 = no collision found
  auto add_score = [&](int pc, int depth) {
    score[static_cast<size_t>(pc)] += 1.0f / exp2(static_cast<float>(depth - 2));
    auto & slot = min_depth[static_cast<size_t>(pc)];
    if (slot == 0 || depth < slot) slot = depth;
  };
  for (auto & collision : collisions) {
    add_score(collision.step, collision.depth);
    if (collision.step != 0) add_score(edo_steps - collision.step, collision.depth);
  }
  auto max_score = *max_element(score.begin(), score.end());
  if (max_score > 0.0f) {
    for (int pc = 0; pc < edo_steps; pc++) {
      if (score[static_cast<size_t>(pc)] <= 0.0f) continue;
      auto tier = result[static_cast<size_t>(pc)].tier;
      if (tier == PadTier::TONIC || tier == PadTier::FOURTH || tier == PadTier::FIFTH) continue;
      result[static_cast<size_t>(pc)].enharmonic_blend = score[static_cast<size_t>(pc)] / max_score;
      result[static_cast<size_t>(pc)].enharmonic_depth = min_depth[static_cast<size_t>(pc)];
    }
  }

  return result;
}

PadClassification
classifyPad(const vector<PadClassification> & levels, const Basis & basis, int edo_steps, int x, int y, int base_note) {
  auto note = noteForPad(basis, x, y, base_note);
  auto pitch_class = ((note - base_note) % edo_steps + edo_steps) % edo_steps;
  return levels[static_cast<size_t>(pitch_class)];
}

// Rows numbered bottom (y=0) to top (y=7), matching Programmer-mode
// addressing. Kit on the left half (x=0..3), hand/latin percussion on
// the right (x=4..7). Every named family (see percussionFamilyForPad)
// occupies a contiguous rectangle and never wraps across a row boundary
// - the defect the earlier row-linear table had. Kick/snare sit in the
// bottom-left corner (easiest reach) in the order they're actually
// played; toms ascend left-to-right then bottom-to-top so a fill is a
// diagonal sweep; bongos/timbales share row 0 as low-to-high pairs.
// Cowbell (56) sits with the agogos rather than the kit, since in
// practice it's played as part of a latin cluster far more often than as
// a kit accessory. A perfect bijection onto Note.h's percussion_names[]
// range (GM values 27-82, 56 sounds) - unlike the standard 47-note GM
// percussion range (35-81) this covers, it also places the 8 Roland-GS
// electronic-kit hits (27-34) and Shaker (82) that this engine has
// always supported, so switching to this family-clustered layout doesn't
// drop any previously-playable sound. 8 pads remain unassigned, as
// small gaps between families rather than confined to one row.
static const int PERCUSSION_TABLE[8][8] = {
  { 35, 36, 37, 38, 61, 60, 66, 65 },
  { 40, 39, -1, -1, 64, 62, 63, -1 },
  { 42, 44, 46, -1, 68, 67, 56, -1 },
  { 41, 43, 45, 82, 69, 70, 73, 74 },
  { 47, 48, 50, -1, 75, 76, 77, -1 },
  { 49, 57, 55, 52, 78, 79, 71, 72 },
  { 51, 59, 53, -1, 80, 81, 33, 34 },
  { 54, 58, 27, 28, 29, 30, 31, 32 },
};

int
percussionNoteForPad(int x, int y) {
  if (x < 0 || x > 7 || y < 0 || y > 7) return -1;
  return PERCUSSION_TABLE[y][x];
}

PercussionFamily
percussionFamilyForPad(int x, int y) {
  auto note = percussionNoteForPad(x, y);
  if (note < 0) return PercussionFamily::UNUSED;

  switch (note) {
  case 35: case 36: case 37: case 38: case 39: case 40:
    return PercussionFamily::CORE;
  case 42: case 44: case 46:
    return PercussionFamily::HI_HAT;
  case 41: case 43: case 45: case 47: case 48: case 50:
    return PercussionFamily::TOMS;
  case 49: case 51: case 52: case 53: case 55: case 57: case 59:
    return PercussionFamily::CYMBALS;
  case 54: case 58:
    return PercussionFamily::KIT_ACCESSORIES;
  case 60: case 61: case 62: case 63: case 64: case 65: case 66:
    return PercussionFamily::LATIN_DRUMS;
  case 56: case 67: case 68:
    return PercussionFamily::LATIN_METAL;
  case 69: case 70: case 73: case 74: case 82:
    return PercussionFamily::SHAKERS;
  case 75: case 76: case 77:
    return PercussionFamily::WOODS;
  case 71: case 72: case 78: case 79: case 80: case 81:
    return PercussionFamily::CUICA_WHISTLE;
  case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34:
    return PercussionFamily::ELECTRONIC;
  default:
    return PercussionFamily::UNUSED;
  }
}

int
clampOctave(int octave, int delta) {
  auto result = octave + delta;
  if (result < 0) return 0;
  if (result > 9) return 9;
  return result;
}

int
advanceTrackIndex(int current_or_unassigned, int delta, int num_tracks) {
  if (num_tracks <= 0) return 0;
  auto result = current_or_unassigned + delta;
  if (result < 0) return 0;
  if (result > num_tracks - 1) return num_tracks - 1;
  return result;
}

}
