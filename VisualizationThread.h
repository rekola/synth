#ifndef _VISUALIZATIONTHREAD_H_
#define _VISUALIZATIONTHREAD_H_

#include "EventHandler.h"
#include "dsp/SpectrumAnalyzer.h"
#include "dsp/DiracAnalyzer.h"

#include <memory>

class Controller;

// Runs real signal-analysis work (the spectrum-FFT feeding the terminal's
// live chart, and DirAC directional analysis feeding the heatmap - see
// plans/dirac-heatmap-scope.md) on its own dedicated thread, spawned by
// UI::start() alongside the audio thread - deliberately neither the
// real-time audio thread (which must never be blocked by FFT-sized work)
// nor the UI/main thread (which must stay responsive to input). Receives
// raw audio as AudioBlockEvents (Controller::getVisualizationQueue(), audio
// thread -> here) and sends results back as VisualizationResultEvents
// (Controller::getUIEventQueue(), here -> UI thread) - the UI thread does
// no analysis of its own, it only displays whatever this thread already
// computed.
class VisualizationThread : public EventHandler {
 public:
  explicit VisualizationThread(Controller * controller) : controller_(controller) { }

  // Sizes the spectrum FFT's analysis window to the largest whole multiple
  // of frame_count (the audio engine's own block size) that still fits
  // within 100ms of audio at sample_rate - staying in whole blocks means
  // SpectrumAnalyzer's own accumulate-until-full threshold (addData())
  // lands exactly on an AudioBlockEvent boundary rather than splitting one,
  // and targeting ~100ms means the chart refreshes roughly 10 times a
  // second, which is plenty for a value a person is just glancing at.
  // Also constructs the DirAC analyzer (it takes sample_rate at
  // construction, not a separate setSize()-style call).
  void configure(int sample_rate, int frame_count) {
    int size = 0;
    for (; size + frame_count <= sample_rate / 10; size += frame_count) { }
    spectrum_.setSize(size);
    dirac_ = std::make_unique<DiracAnalyzer>(sample_rate);
  }

  void handleAudioBlockEvent(AudioBlockEvent & ev) override;

  // Poll loop over Controller::getVisualizationQueue(), mirroring
  // Player::play()'s own poll()+drain shape. Returns once an AudioBlockEvent
  // carrying an empty AudioBuffer (the terminate sentinel - see
  // AudioBlockEvent.h) is received.
  void run();

 private:
  Controller * controller_;
  SpectrumAnalyzer spectrum_;
  std::unique_ptr<DiracAnalyzer> dirac_; // constructed by configure() - needs sample_rate, unknown at this object's own construction time
  int dirac_last_pushed_frame_ = 0;      // SS1's every-3rd-analysis-frame render-throttle - see handleAudioBlockEvent()
  bool terminate_ = false;
};

#endif
