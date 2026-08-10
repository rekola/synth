#ifndef _VISUALIZATIONRESULTEVENT_H_
#define _VISUALIZATIONRESULTEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "dsp/DiracAnalyzer.h"

#include <array>
#include <string>
#include <vector>

// VisualizationThread -> UI thread: analysis results computed off both the
// real-time audio thread and the UI thread (see VisualizationThread.h).
// Pushed onto the same ui_event_queue PlaybackEvent/RecordEvent already
// share - EventQueue/EventHandler's dispatch-by-dynamic-type already means
// mixing producers onto one queue is the existing pattern, not a new one.
// The FFT vector and DirAC grid are independently optional - one event may
// carry either, both, or (rarely) neither, since they update at different
// rates (see VisualizationThread.h); hasDiracGrid()/getFFT().empty() are
// how a consumer tells which are actually present in a given event. The
// raw-channel loudness/meter label, by contrast, are always present on
// every real event (VisualizationThread::handleAudioBlockEvent() sets
// them unconditionally, every audio block - see that method) - there is
// no equivalent "has" check for them.
class VisualizationResultEvent : public Event {
public:
  void dispatch(EventHandler & evh) override { evh.handleVisualizationResultEvent(*this); }

  // FFT magnitude-dB vector for the live spectrum chart, computed by
  // VisualizationThread (see VisualizationThread.h) - never by the audio
  // thread, which must stay free of FFT-sized work.
  void setFFT(std::vector<float> data) { fft_data_ = std::move(data); }
  const std::vector<float> & getFFT() const { return fft_data_; }

  // Raw, pre-mixdown per-channel loudness (ambisonic bus channels,
  // however many are active, then always AuxA/AuxB last) - feeds the
  // UI's raw-channel volume meter. Variable length, unlike the final
  // decoded output's fixed channel count. Computed here (not on the
  // real-time audio thread that produces the samples it's derived from
  // - see Player.cpp/VisualizationThread.cpp) since it's a real
  // DSP-shaped scan (AudioBuffer::calculateLoudness()), the same reasoning
  // that already keeps the FFT/DirAC work off that thread.
  void setChannelLoudness(std::vector<float> loudness) { channel_loudness_ = std::move(loudness); }
  const std::vector<float> & getChannelLoudness() const { return channel_loudness_; }

  // Legend for the raw-channel meter (e.g. "M1-9 A") - computed alongside
  // getChannelLoudness() above, from the same pre-padding regular channel
  // count (which getChannelLoudness()'s total size alone can't
  // distinguish - e.g. a padded mono channel and an unpadded stereo pair
  // both total 4 once AuxA/AuxB are appended).
  void setMeterLabel(std::string label) { meter_label_ = std::move(label); }
  const std::string & getMeterLabel() const { return meter_label_; }

  // The DirAC directional heatmap's smoothed grid and per-band diffuse-haze
  // scalars (DiracAnalyzer::getGrid()/getDiffuseEnergy() - see that class
  // and plans/dirac-heatmap-scope.md SS6 for the rendering formula that
  // combines them). Not yet consumed by anything (the heatmap widget
  // itself doesn't exist yet) - transmitted regardless, since
  // VisualizationThread's own throttling/computation is already complete
  // and tested independently of the widget that will eventually read it.
  void setDiracGrid(std::array<float, DiracAnalyzer::kGridSize> grid, std::array<float, DiracAnalyzer::kNumBands> diffuse_energy) {
    dirac_grid_ = grid;
    dirac_diffuse_energy_ = diffuse_energy;
    has_dirac_grid_ = true;
  }
  bool hasDiracGrid() const { return has_dirac_grid_; }
  const std::array<float, DiracAnalyzer::kGridSize> & getDiracGrid() const { return dirac_grid_; }
  const std::array<float, DiracAnalyzer::kNumBands> & getDiracDiffuseEnergy() const { return dirac_diffuse_energy_; }

private:
  std::vector<float> fft_data_;
  std::vector<float> channel_loudness_;
  std::string meter_label_;
  bool has_dirac_grid_ = false;
  std::array<float, DiracAnalyzer::kGridSize> dirac_grid_ {};
  std::array<float, DiracAnalyzer::kNumBands> dirac_diffuse_energy_ {};
};

#endif
