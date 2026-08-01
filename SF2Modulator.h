#ifndef _SF2MODULATOR_H_
#define _SF2MODULATOR_H_

#include <cstdint>
#include <vector>

// Pure computation for SF2 (SoundFont 2.01) modulator connections - no
// dependency on SoundFont.cpp's tsf_* file-parsing internals, so this is
// directly unit-testable without loading a real SF2 file. Mirrors the
// LaunchpadLayout.h/.cpp split (pure math vs. device/file-specific code)
// used elsewhere in this codebase.
namespace SF2Mod {

  // SF2.01 8.2.1's General Controller palette (used when a source's CC
  // flag is 0) - only the values this engine actually recognizes are
  // named; the spec defines more, but nothing else here reads them.
  enum class GeneralController : uint16_t {
    NoController = 0,
    NoteOnVelocity = 2,
    NoteOnKeyNumber = 3,
    PolyPressure = 10,
    ChannelPressure = 13,
    PitchWheel = 14,
    PitchWheelSensitivity = 16,
    Link = 127,
  };

  // SF2.01 9.2's four source-curve shapes. Note the spec's "concave"/
  // "convex" naming is the audio-engineering taper sense (concave = slow
  // near 0, fast near 1, like a log-taper pot), not the pure-math sense
  // (mathematically that shape is convex) - a well-known point of
  // confusion when implementing this.
  enum class CurveType : uint8_t { Linear = 0, Concave = 1, Convex = 2, Switch = 3 };

  // Decoded sfModSrcOper/sfModAmtSrcOper (raw 16-bit packed field, SF2.01
  // 8.2.1): bits 0-6 index (a MIDI CC number if isMidiCC, else a
  // GeneralController value), bit 7 CC flag, bit 8 direction (1 =
  // decreasing, i.e. max reading -> 0), bit 9 polarity (1 = bipolar,
  // output remapped to [-1,1] instead of [0,1]), bits 10-15 curve type.
  struct Source {
    bool isMidiCC = false;
    uint8_t index = 0;
    bool decreasing = false;
    bool bipolar = false;
    CurveType curve = CurveType::Linear;
  };
  Source parseModSource(uint16_t raw);

  // Maps a normalized controller reading (x01 in [0,1], 0 = controller at
  // its minimum, 1 = at its maximum - e.g. a 7-bit MIDI value / 127) to
  // the SF2.01 9.2 source-curve output, applying direction, curve shape,
  // and polarity from `source`. Returns a value in [0,1] (unipolar
  // sources) or [-1,1] (bipolar sources). The concave/convex formulas
  // below satisfy the spec's boundary conditions (0->0, 1->1) and general
  // shape (audio-taper curves) but their exact numeric constants are a
  // best-effort reconstruction, not transcribed from the primary SF2.01
  // spec text - cross-check against the spec PDF or a reference
  // implementation (e.g. fluidsynth's fluid_conc_tab/fluid_convex_tab)
  // before relying on precise curve shape, though linear/switch (the
  // shapes real hand-authored SF2 modulators typically use, e.g.
  // TimGM6mb.sf2's own channel-pressure connections) are exact.
  float applySourceCurve(float x01, const Source & source);

  // A single SF2 modulator connection - src/dest/amtSrc are raw packed
  // sfModSrcOper-style fields (dest is a plain SF2 generator index, 0-58,
  // not source-encoded), amount is the signed sfModAmount, trans is the
  // raw sfModTransOper (0 = linear passthrough, 2 = absolute value).
  struct Connection {
    uint16_t src = 0, dest = 0, amtSrc = 0, trans = 0;
    int16_t amount = 0;
  };

  // True iff src/dest/amtSrc/trans all match - the SF2.01 9.5.1 "identical
  // modulator" identity used to decide when one connection replaces
  // another rather than the two simply coexisting.
  bool sameIdentity(const Connection & a, const Connection & b);

  // "base, with any entry sharing an identity with one in overrides
  // replaced by that override" - the general zone-combination rule this
  // engine reuses for the SF2.01 7.4 global-zone merge and the 9.5.1
  // instrument/preset merge.
  std::vector<Connection> mergeModulators(const std::vector<Connection> & base, const std::vector<Connection> & overrides);

  // True iff `c` is sourced from channel pressure with no secondary
  // amount-source scaling (amtSrc = No Controller) - the shape every
  // hand-authored "channel pressure -> X" modulator takes, and the only
  // one this engine has a live per-voice value for. Used at runtime to
  // select which of a region's own (file-authored) connections to
  // evaluate.
  bool isChannelPressureSourced(const Connection & c);

  // Evaluates `c` (which must satisfy isChannelPressureSourced - callers
  // check that first) against a normalized channel-pressure reading
  // (pressure01 in [0,1]): amount * curve(pressure), with the absolute-
  // value transform applied when trans == 2. The result is in whatever
  // units the destination generator (c.dest) uses - the caller decides
  // which generator's running total to add it into.
  float evaluateChannelPressureModulator(const Connection & c, float pressure01);

}

#endif
