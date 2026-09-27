#ifndef _PADSYNTHVOICE_H_
#define _PADSYNTHVOICE_H_

#include "InstrumentVoice.h"
#include "PadSynthWavetable.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/NoteCoordinate.h"

#include <cmath>
#include <memory>
#include <vector>

class PadSynthVoice : public InstrumentVoice {
public:
  PadSynthVoice(const ChannelConfiguration & config, const SphericalPosition & position, float detune,
                std::shared_ptr<PadSynthWavetable> table, float level,
                const SendLevels & sends = {}, const NoteCoordinate & note_coord = {})
    : InstrumentVoice(config, position, detune, sends, note_coord), table_(std::move(table)), level_(level) {
  }

  AudioBuffer render(int frames) override {
    float gain = decibelsToGain(getGainDB()) * level_;

    const std::vector<float> & wave = table_->getTable(getFrequency());
    float table_base_freq = table_->tableBaseFrequency(getFrequency());
    double table_len = static_cast<double>(wave.size());

    // Table-samples advanced per output sample - the ratio of this note's
    // real pitch to the table's own base pitch (tableBaseFrequency()),
    // exactly like a sampler resampling a single-cycle waveform to a new
    // pitch. This deliberately never reintroduces off-tuning-step
    // partials: every partial in the table was already snapped to a scale
    // step in cents-space at generation time (SpectralBandProfile.h's
    // tuningMatchedPartialRatio()), and resampling multiplies every
    // partial's absolute frequency by this same single ratio - which
    // preserves each partial's own cents-distance from the fundamental
    // exactly, it doesn't recompute or re-snap anything. A partial that
    // was, say, 3 cents flat of a plain harmonic at generation time is
    // still exactly 3 cents flat of that same (now-transposed) harmonic
    // after resampling, whatever pitch the table is played back at within
    // its own octave region.
    double rate = static_cast<double>(getFrequency()) / static_cast<double>(table_base_freq);

    // getSourceSamplePosition()/stepForward() are InstrumentVoice's shared
    // phase-accumulator mechanism (see its own doc comment): it tracks
    // elapsed_cycles * sampleRate, so elapsed_cycles = position/sampleRate
    // - converting that into a table-read position (table samples, not
    // cycles) needs one more factor of sampleRate/table_base_freq, which
    // cancels the first division exactly, leaving position/table_base_freq.
    double table_pos = getSourceSamplePosition() / static_cast<double>(table_base_freq);

    if (static_cast<int>(dry_.size()) != frames) dry_.resize(static_cast<size_t>(frames));

    for (int k = 0; k < frames; k++) {
      double idx = std::fmod(table_pos, table_len);
      if (idx < 0.0) idx += table_len;

      size_t i0 = static_cast<size_t>(idx);
      size_t i1 = (i0 + 1 < wave.size()) ? i0 + 1 : 0;
      float frac = static_cast<float>(idx - static_cast<double>(i0));
      float sample = wave[i0] + (wave[i1] - wave[i0]) * frac; // linear interpolation

      dry_[static_cast<size_t>(k)] = gain * sample;

      table_pos += rate;
    }

    stepForward(frames);

    return encodePosition(dry_.data(), frames);
  }

private:
  std::shared_ptr<PadSynthWavetable> table_;
  float level_;
  std::vector<float> dry_;
};

#endif
