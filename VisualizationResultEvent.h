#ifndef _VISUALIZATIONRESULTEVENT_H_
#define _VISUALIZATIONRESULTEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "dsp/DiracAnalyzer.h"

#include <array>
#include <vector>

// VisualizationThread -> UI thread: analysis results computed off both the
// real-time audio thread and the UI thread (see VisualizationThread.h).
// Pushed onto the same ui_event_queue PlaybackEvent/RecordEvent already
// share - EventQueue/EventHandler's dispatch-by-dynamic-type already means
// mixing producers onto one queue is the existing pattern, not a new one.
// The FFT vector and DirAC grid are independently optional - one event may
// carry either, both, or (rarely) neither, since they update at different
// rates (see VisualizationThread.h); hasDiracGrid()/getFFT().empty() are
// how a consumer tells which are actually present in a given event.
class VisualizationResultEvent : public Event {
public:
  void dispatch(EventHandler & evh) override { evh.handleVisualizationResultEvent(*this); }

  // FFT magnitude-dB vector for the live spectrum chart, computed by
  // VisualizationThread (see VisualizationThread.h) - never by the audio
  // thread, which must stay free of FFT-sized work.
  void setFFT(std::vector<float> data) { fft_data_ = std::move(data); }
  const std::vector<float> & getFFT() const { return fft_data_; }

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
  bool has_dirac_grid_ = false;
  std::array<float, DiracAnalyzer::kGridSize> dirac_grid_ {};
  std::array<float, DiracAnalyzer::kNumBands> dirac_diffuse_energy_ {};
};

#endif
