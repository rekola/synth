#include "TestFramework.h"

#include "../VisualizationThread.h"
#include "../AudioBlockEvent.h"
#include "../VisualizationResultEvent.h"
#include "../Controller.h"
#include "../dsp/DiracAnalyzer.h"

#include <cmath>

TEST(visualization_thread_audio_block_event_produces_fft_result) {
  Controller controller{ChannelConfiguration()};
  VisualizationThread thread(&controller);

  // sample_rate/10 = 300, so configure()'s "largest whole multiple of
  // frame_count that fits in 100ms" loop lands on exactly one multiple
  // (256) - a single AudioBlockEvent of that many frames already satisfies
  // SpectrumAnalyzer::addData()'s fill threshold, so the result is ready
  // after just one handleAudioBlockEvent() call.
  int sample_rate = 3000, frame_count = 256;
  thread.configure(sample_rate, frame_count);

  SampleData master(2, frame_count);
  for (int i = 0; i < frame_count; i++) {
    float v = sinf(static_cast<float>(i) * 0.2f);
    master.getChannelData(0)[i] = v;
    master.getChannelData(1)[i] = v;
  }

  // No real DirAC content needed for this test - an empty raw bus is a
  // safe, deliberate no-op for DiracAnalyzer::process() (0 frames, nothing
  // to accumulate), same as the terminate sentinel's empty master below
  // just for a different field.
  AudioBlockEvent ev(std::move(master), SampleData());
  thread.handleAudioBlockEvent(ev);

  // EventQueue::hasEvents() only reflects bytes already read off the
  // wakeup socket (inside pop()'s own loop) - it can't see a just-pushed
  // event until something has actually popped at least once, so pop()
  // directly rather than checking hasEvents() first.
  auto result_event = controller.getUIEventQueue().pop();
  auto * result = dynamic_cast<VisualizationResultEvent *>(result_event.get());
  CHECK(result != nullptr);
  if (result) {
    CHECK(!result->getFFT().empty());
  }
}

TEST(visualization_thread_dirac_grid_delivered_after_throttle_threshold) {
  Controller controller{ChannelConfiguration()};
  VisualizationThread thread(&controller);
  thread.configure(44100, 256); // FFT size ~4352 - far bigger than this test's own master, so it never fires here

  // DiracAnalyzer::kFFTSize (1024) + 2*kHopSize (512) = 2048 samples is the
  // minimum that advances its internal analysis-frame counter by 3 in one
  // process() call - exactly VisualizationThread's own render-throttle
  // (SS1) - so a single handleAudioBlockEvent() call is enough to trigger
  // delivery.
  int dirac_frames = DiracAnalyzer::kFFTSize + 2 * DiracAnalyzer::kHopSize;
  SampleData master(2, 10); // small and unrelated to the FFT path (see configure() above)
  SampleData raw_bus(1, dirac_frames); // W-only is enough to exercise the throttle itself
  for (int i = 0; i < dirac_frames; i++) {
    raw_bus.getChannelData(0)[i] = sinf(static_cast<float>(i) * 0.05f);
  }

  AudioBlockEvent ev(std::move(master), std::move(raw_bus));
  thread.handleAudioBlockEvent(ev);

  auto result_event = controller.getUIEventQueue().pop();
  auto * result = dynamic_cast<VisualizationResultEvent *>(result_event.get());
  CHECK(result != nullptr);
  if (result) CHECK(result->hasDiracGrid());
}

TEST(visualization_thread_terminate_sentinel_produces_no_result) {
  // An empty SampleData (the sentinel AudioBlockEvent::run() watches for,
  // see AudioBlockEvent.h) must not be treated as real audio data - no
  // VisualizationResultEvent should come out of it.
  Controller controller{ChannelConfiguration()};
  VisualizationThread thread(&controller);
  thread.configure(3000, 256);

  AudioBlockEvent terminate_ev{SampleData(), SampleData()};
  thread.handleAudioBlockEvent(terminate_ev);

  CHECK(!controller.getUIEventQueue().hasEvents());
}
