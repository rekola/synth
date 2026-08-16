#include "LaunchpadLayout.h"

#include <cmath>

using namespace std;

namespace LaunchpadLayout {

namespace {
  // Starting points for the hue tree - tunable, not load-bearing for pitch
  // classification (only the LED-coloring result). TONIC and FOURTH/FIFTH
  // are fixed, prominent pop-out hues (yellow-leaning-red, amber-leaning-
  // red) - confirmed against real Launchpad X hardware that green LEDs
  // read as much brighter/more prominent than blue or purple ones
  // regardless of the saturation value sent, so both of this pair are
  // kept away from green. The RECURSIVE-tier hue drift is confined to a
  // separate blue/violet/magenta band (see kRecursiveBaseHue below) that
  // never reaches anywhere near green - LaunchpadManager's
  // consonanceColor() additionally caps how deep this drift is allowed to
  // visibly differentiate (depth 5+ all render as one flat, deliberately
  // unprominent color - see its own comment) since even within blue/
  // violet, closely-spaced hues were hard to tell apart on real hardware.
  // FOURTH and FIFTH get their own close-but-distinct hues (a small split
  // around the same amber center, same spirit as kDepth3HueOffset below) -
  // a single shared hue made the two tiers indistinguishable at a glance,
  // so each gets its own, even though they're close.
  constexpr float kTonicHue = 50.0f; // yellow, nudged toward red
  constexpr float kFourthFifthCenterHue = 18.0f; // amber, nudged further toward red
  constexpr float kFourthFifthHueOffset = 6.0f;
  constexpr float kFourthHue = kFourthFifthCenterHue - kFourthFifthHueOffset;
  constexpr float kFifthHue = kFourthFifthCenterHue + kFourthFifthHueOffset;
  // Center of the RECURSIVE-tier hue drift - blue/violet, as far from the
  // amber/yellow prominent hues as this drift's range ever reaches (see
  // kDepth3HueOffset/kDepth4HueOffset below).
  constexpr float kRecursiveBaseHue = 270.0f;
  // Depths 3 and 4 are the only two RECURSIVE depths that reach the
  // device as distinct hues (consonanceColor() flattens depth 5+ to one
  // flat gray) - fixed, deliberately asymmetric offsets rather than a
  // geometric decay (which was tuned for smoothly-shrinking steps across
  // many depths). Real-hardware testing tuned these in opposite
  // directions: major/minor at the *same* depth (e.g. E vs. Eb, both
  // depth 3) should read as close/related, so kDepth3HueOffset is small;
  // depth 3 vs. depth 4 within the *same* family (e.g. A vs. B, both
  // major) should read as clearly different depths, so kDepth4HueOffset
  // is much larger than kDepth3HueOffset, not just a smaller geometric
  // step beyond it. kRecursiveBaseHue +/- kDepth4HueOffset is [195,345],
  // still comfortably inside blue/violet/magenta - clear of both green
  // (~90-160, confirmed too prominent on this hardware regardless of
  // saturation) and the warm tonic/fourth/fifth hues (~0-70).
  constexpr float kDepth3HueOffset = 15.0f;
  constexpr float kDepth4HueOffset = 75.0f;
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
