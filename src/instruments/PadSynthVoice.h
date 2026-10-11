#ifndef _PADSYNTHVOICE_H_
#define _PADSYNTHVOICE_H_

#include "InstrumentVoice.h"
#include "PadSynthTable.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/NoteCoordinate.h"
#include "../dsp/HashField.h"

#include <cmath>
#include <memory>
#include <vector>

// Where in the table a note starts, as a fraction of the table's length.
inline float padSynthStartFraction(const NoteCoordinate & note_coord) {
  constexpr uint64_t kStartSalt = 0x5BD1E9955BD1E995ull;
  return HashField(kStartSalt).unit(note_coord.toHashCoord(), paramId("padsynth_start"));
}

class PadSynthVoice : public InstrumentVoice {
public:
  PadSynthVoice(const ChannelConfiguration & config, const SphericalPosition & position, float detune,
                std::shared_ptr<PadSynthTable> table, float level,
                const SendLevels & sends = {}, const NoteCoordinate & note_coord = {})
    : InstrumentVoice(config, position, detune, sends, note_coord), table_(std::move(table)), level_(level),
      start_fraction_(padSynthStartFraction(note_coord)) {
  }

  void playNote(float frequency, float velocity, int note_value) override {
    bool fresh = getFrequency() == 0.0f;
    InstrumentVoice::playNote(frequency, velocity, note_value);
    if (fresh) {
      wave_ = nullptr;
      selectTable();
    }
  }

  AudioBuffer render(int frames) override {
    float gain = decibelsToGain(getGainDB()) * level_;

    selectTable(); // normally already done at note-on
    const std::vector<float> & wave = *wave_;
    float table_base_freq = table_base_freq_;
    double table_len = static_cast<double>(wave.size());

    // Table-samples advanced per output sample - the ratio of this note's
    // real pitch to the table's own base pitch (tableBaseFrequency()),
    // exactly like a sampler resampling a single-cycle waveform to a new
    // pitch. Resampling multiplies every partial's absolute
    // frequency by this same single ratio, which preserves each partial's
    // cents-distance from the fundamental exactly.
    double rate = static_cast<double>(getFrequency()) / static_cast<double>(table_base_freq);

    // The table is a long noise-like loop, so a note may start anywhere in
    // it; the start is hashed from the note's coordinate, so simultaneous
    // notes and detuned copies read unrelated stretches of it.
    if (!started_) {
      table_pos_ = static_cast<double>(start_fraction_) * table_len;
      started_ = true;
    }
    double table_pos = table_pos_;

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

    table_pos_ = std::fmod(table_pos, table_len);

    return encodePosition(dry_.data(), frames);
  }

private:
  // The region is chosen once, at note-on, and kept for the whole note: a
  // pitch change across a region boundary would otherwise swap tables at
  // the same position and click.
  void selectTable() {
    if (wave_) return;
    wave_ = &table_->getTable(getFrequency());
    table_base_freq_ = table_->tableBaseFrequency(getFrequency());
  }

  std::shared_ptr<PadSynthTable> table_;
  const std::vector<float> * wave_ = nullptr;
  float table_base_freq_ = 1.0f;
  float level_;
  float start_fraction_; // where in the table the note starts, as a fraction of its length
  double table_pos_ = 0.0;
  bool started_ = false;
  std::vector<float> dry_;
};

#endif
