#ifndef _EQUALIZEREDITORMODEL_H_
#define _EQUALIZEREDITORMODEL_H_

#include "../effects/Equalizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// The toolkit-agnostic half of the equalizer editor: the plot's axes and the
// edits a key or a drag makes to a band. A backend draws the curve and the
// band table and feeds its input through these.
namespace eqeditor {

using Band = Equalizer::Band;

// The plot spans kMinFreq..kMaxFreq on a log axis and +-kPlotDb vertically.
constexpr float kPlotDb = 24.0f;

inline float freqToUnit(float hz) {
  return std::log2(std::clamp(hz, Equalizer::kMinFreq, Equalizer::kMaxFreq) / Equalizer::kMinFreq) / std::log2(Equalizer::kMaxFreq / Equalizer::kMinFreq);
}
inline float unitToFreq(float u) {
  return Equalizer::kMinFreq * std::exp2(std::clamp(u, 0.0f, 1.0f) * std::log2(Equalizer::kMaxFreq / Equalizer::kMinFreq));
}
// 0 at the bottom of the plot, 1 at the top.
inline float gainToUnit(float db) { return (std::clamp(db, -kPlotDb, kPlotDb) + kPlotDb) / (2.0f * kPlotDb); }
inline float unitToGain(float u) { return std::clamp(u, 0.0f, 1.0f) * 2.0f * kPlotDb - kPlotDb; }

// Where a band's marker sits; a band with no gain sits on the 0 dB line.
inline float markerGainUnit(const Band & band) { return gainToUnit(band.hasGain() ? band.gain_db : 0.0f); }

// A key or drag changes a band to the returned value; setBand() clamps it.
inline Band withGainDelta(Band band, float db) {
  if (band.hasGain()) band.gain_db = std::clamp(band.gain_db + db, Equalizer::kMinGainDb, Equalizer::kMaxGainDb);
  return band;
}
inline Band withFreqOctaves(Band band, float octaves) {
  band.freq = std::clamp(band.freq * std::exp2(octaves), Equalizer::kMinFreq, Equalizer::kMaxFreq);
  return band;
}
inline Band withQScale(Band band, float factor) {
  band.q = std::clamp(band.q * factor, Equalizer::kMinQ, Equalizer::kMaxQ);
  return band;
}
// Moves to a position on the plot (units as above); the gain follows only
// for types that have one.
inline Band withPlotPosition(Band band, float x_unit, float y_unit) {
  band.freq = unitToFreq(x_unit);
  if (band.hasGain()) band.gain_db = std::round(unitToGain(y_unit) * 2.0f) / 2.0f;
  return band;
}

inline const FilterType kTypeCycle[] = { FilterType::highpass, FilterType::lowshelf, FilterType::peak, FilterType::notch,
                                        FilterType::bandpass, FilterType::highshelf, FilterType::lowpass };
inline Band withNextType(Band band, int step) {
  constexpr int n = static_cast<int>(sizeof(kTypeCycle) / sizeof(kTypeCycle[0]));
  int at = 0;
  for (int i = 0; i < n; i++) if (kTypeCycle[i] == band.type) at = i;
  band.type = kTypeCycle[((at + step) % n + n) % n];
  return band;
}

// The band whose marker is nearest a point on the plot, measured in the
// plot's own cells (`cols` x `rows`), so a click lands on what looks closest.
inline int nearestBand(const Equalizer & eq, float x_unit, float y_unit, int cols, int rows) {
  int best = 0;
  float best_distance = 1e30f;
  for (int i = 0; i < Equalizer::kBands; i++) {
    float dx = (freqToUnit(eq.getBand(i).freq) - x_unit) * static_cast<float>(cols);
    float dy = (markerGainUnit(eq.getBand(i)) - y_unit) * static_cast<float>(rows) * 2.0f; // cells are about twice as tall as wide
    float d = dx * dx + dy * dy;
    if (d < best_distance) { best_distance = d; best = i; }
  }
  return best;
}

inline const char * typeName(FilterType type) {
  switch (type) {
  case FilterType::lowpass: return "LowPass";
  case FilterType::highpass: return "HighPass";
  case FilterType::bandpass: return "BandPass";
  case FilterType::notch: return "Notch";
  case FilterType::peak: return "Peak";
  case FilterType::lowshelf: return "LowShelf";
  case FilterType::highshelf: return "HiShelf";
  }
  return "";
}

inline std::string freqText(float hz) {
  char text[24];
  if (hz >= 1000.0f) std::snprintf(text, sizeof(text), hz >= 10000.0f ? "%.0f kHz" : "%.2f kHz", static_cast<double>(hz) / 1000.0);
  else std::snprintf(text, sizeof(text), "%.0f Hz", static_cast<double>(hz));
  return text;
}
inline std::string gainText(const Band & band) {
  if (!band.hasGain()) return "-";
  char text[24];
  std::snprintf(text, sizeof(text), "%+.1f dB", static_cast<double>(band.gain_db));
  return text;
}
inline std::string qText(float q) {
  char text[24];
  std::snprintf(text, sizeof(text), "Q %.2f", static_cast<double>(q));
  return text;
}

}

#endif
