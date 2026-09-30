#ifndef _PADSYNTH_H_
#define _PADSYNTH_H_

#include "Instrument.h"
#include "PadSynthTable.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

#include <cstdint>
#include <memory>
#include <string>

// A PADsynth-resynthesis oscillator - modeled directly on Oscillator's own
// shape (same base class, same loadParameters()/storeParameters()/
// playNote() pattern), but reading from a PadSynthTable instead
// of computing an analytic waveform per sample.
class PadSynth : public Instrument {
 public:
  PadSynth() { }

  const char * getElementName() const override { return "padsynth"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}, bool needs_decorrelation = false) const override;

  // Forces the wavetable covering `note_value`'s own pitch region to
  // (re)build now, off any real-time audio thread, instead of leaving it
  // to happen lazily inside the first voice's first render() call - table
  // generation is a real, non-trivial cost, and both playNote() and
  // render() run on the real-time audio thread (Player::
  // startPreviewNote(), in particular - reported as an audible click every
  // time a not-yet-built preset/register is previewed). Callable from
  // anywhere that already has the right ChannelConfiguration/Tuning before
  // real-time playback starts - see InstrumentLibrary.cpp's own call site,
  // right after registerLibraryInstruments() in Controller's constructor
  // (a startup-time context, not the audio thread). Only warms the one
  // region `note_value` falls in - a note played in a different register
  // still builds lazily on first use, same as before.
  void prewarm(const ChannelConfiguration & config, Tuning tuning, int note_value) const;

 private:
  void ensureWavetable(const ChannelConfiguration & config, Tuning tuning) const;

  // Built lazily on first playNote() (mutable, matching SampleContent.h's
  // own waveform_peaks_/stretched_buffer_ lazy-cache precedent) since
  // table generation needs the real output sample rate, which only a
  // ChannelConfiguration (playNote()'s own first argument) carries -
  // Instrument::prepare() only ever gets an InstrumentProvider, with no
  // sample rate of its own to build against. One instance is shared by
  // every voice/note played through this node - rebuilt if the output
  // sample rate or the song's own tuning has changed since the table was
  // built (e.g. --samplerate, or a song-tuning change, between one
  // playNote() and the next) - neither happens mid-song in practice but
  // both are checked rather than assumed.
  mutable std::shared_ptr<PadSynthTable> wavetable_;
  mutable Tuning wavetable_tuning_ = Tuning::TET31;

  std::string preset_ = "strings";
  bool tuning_matched_ = true;
  float level_ = 1.0f;
  uint64_t seed_ = 1;

  // Anchored spectral-envelope remap (dsp/SpectralEnvelopeRemap.h) - XML
  // attributes envelopeAnchor (Hz)/envelopeTracking (float, [0,2]); 0 for
  // either means off, matching every pre-existing preset's own behavior.
  // envelopePostprocess selects "residue"/"stretch" (absent means off,
  // independent of the remap above); envelopePostprocessN/-R/-Amount are
  // its own integer/integer/float [0,1] parameters.
  float envelope_anchor_hz_ = 0.0f;
  float envelope_tracking_ = 0.0f;
  std::string envelope_postprocess_ = "";
  int envelope_postprocess_n_ = 0;
  int envelope_postprocess_r_ = 0;
  float envelope_postprocess_amount_ = 0.0f;
};

#endif
