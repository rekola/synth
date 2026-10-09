#include "TestFramework.h"

#include "../src/ui/EqualizerEditorModel.h"
#include "../src/ui/Spline.h"

#include <cmath>

TEST(equalizer_editor_axes_round_trip) {
  for (float hz : { 20.0f, 100.0f, 1000.0f, 20000.0f }) {
    CHECK_NEAR(eqeditor::unitToFreq(eqeditor::freqToUnit(hz)), hz, hz * 1e-3f);
  }
  CHECK_NEAR(eqeditor::gainToUnit(0.0f), 0.5f, 1e-6f);
  CHECK_NEAR(eqeditor::gainToUnit(eqeditor::kPlotDb), 1.0f, 1e-6f);
  CHECK_NEAR(eqeditor::unitToGain(eqeditor::gainToUnit(7.5f)), 7.5f, 1e-4f);
}

TEST(equalizer_editor_edits_clamp_and_respect_band_type) {
  Equalizer eq;
  auto peak = eq.getBand(2);
  CHECK(peak.hasGain());
  CHECK_NEAR(eqeditor::withGainDelta(peak, 100.0f).gain_db, Equalizer::kMaxGainDb, 1e-6f);
  CHECK_NEAR(eqeditor::withFreqOctaves(peak, 1.0f).freq, peak.freq * 2.0f, 1e-3f);
  CHECK_NEAR(eqeditor::withFreqOctaves(peak, 20.0f).freq, Equalizer::kMaxFreq, 1e-3f);
  CHECK_NEAR(eqeditor::withQScale(peak, 1000.0f).q, Equalizer::kMaxQ, 1e-6f);

  auto hp = eq.getBand(0);
  CHECK(!hp.hasGain());
  CHECK_NEAR(eqeditor::withGainDelta(hp, 6.0f).gain_db, 0.0f, 1e-6f);
  // Dragging a filter band moves only its frequency.
  auto moved = eqeditor::withPlotPosition(hp, eqeditor::freqToUnit(500.0f), 0.9f);
  CHECK_NEAR(moved.freq, 500.0f, 1.0f);
  CHECK_NEAR(moved.gain_db, 0.0f, 1e-6f);
  auto dragged = eqeditor::withPlotPosition(peak, eqeditor::freqToUnit(500.0f), eqeditor::gainToUnit(6.0f));
  CHECK_NEAR(dragged.gain_db, 6.0f, 0.01f);
}

TEST(equalizer_editor_type_cycle_visits_every_type_and_wraps) {
  Equalizer::Band band;
  auto start = band.type;
  int steps = 0;
  do {
    band = eqeditor::withNextType(band, 1);
    steps++;
  } while (band.type != start && steps < 20);
  CHECK(steps == 7);
  CHECK(eqeditor::withNextType(eqeditor::withNextType(band, 1), -1).type == start);
}

TEST(equalizer_editor_nearest_band_follows_the_marker_positions) {
  Equalizer eq;
  auto band = eq.getBand(4); // 2500 Hz
  band.gain_db = 9.0f;
  eq.setBand(4, band);
  CHECK(eqeditor::nearestBand(eq, eqeditor::freqToUnit(2500.0f), eqeditor::gainToUnit(9.0f), 100, 20) == 4);
  CHECK(eqeditor::nearestBand(eq, eqeditor::freqToUnit(100.0f), 0.5f, 100, 20) == 1);
}

TEST(spline_passes_through_flat_knots_and_stays_in_range) {
  auto flat = spline::sample({ 0.4f, 0.4f, 0.4f, 0.4f }, 17);
  for (float v : flat) CHECK_NEAR(v, 0.4f, 1e-5f);
  auto spiky = spline::sample({ 0.0f, 1.0f, 0.0f, 1.0f, 0.0f }, 50);
  for (float v : spiky) CHECK(v >= 0.0f && v <= 1.0f);
}
