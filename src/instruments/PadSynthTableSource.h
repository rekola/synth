#ifndef _PADSYNTHTABLESOURCE_H_
#define _PADSYNTHTABLESOURCE_H_

#include <vector>

// Shared read interface PadSynthVoice reads through, common to both table-
// generation strategies this codebase has: PadSynthWavetable (the
// synthesized-from-scratch Gaussian-band renderer, one table per octave
// region) and ImportedPadSynthTable (the clean-room ZynAddSubFX-faithful
// profile-placement renderer, one table per sample point in an imported
// preset's own sample set - see ImportedPadSynthTable.h). PadSynth picks
// which concrete class to build per preset (PadSynthPresetParams::
// imported_oscillator, PadSynthPresets.h).
class PadSynthTableSource {
 public:
  virtual ~PadSynthTableSource() = default;

  // Returns the table covering f0's own region (an octave region for
  // PadSynthWavetable, the nearest sample point for ImportedPadSynthTable),
  // building and caching it on first use.
  virtual const std::vector<float> & getTable(float f0) const = 0;

  // The region's own reference frequency (Hz) that getTable(f0)'s table
  // was actually generated at - see PadSynthVoice.h's own render() for how
  // a voice resamples from this to its note's real pitch.
  virtual float tableBaseFrequency(float f0) const = 0;

  virtual int getSampleRate() const = 0;
};

#endif
