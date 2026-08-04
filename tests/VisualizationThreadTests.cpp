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
  AudioBlockEvent ev(std::move(master), SampleData(), SampleData(), SampleData());
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

  AudioBlockEvent ev(std::move(master), std::move(raw_bus), SampleData(), SampleData());
  thread.handleAudioBlockEvent(ev);

  auto result_event = controller.getUIEventQueue().pop();
  auto * result = dynamic_cast<VisualizationResultEvent *>(result_event.get());
  CHECK(result != nullptr);
  if (result) CHECK(result->hasDiracGrid());
}

TEST(visualization_thread_computes_channel_loudness_and_meter_label) {
  Controller controller{ChannelConfiguration()};
  VisualizationThread thread(&controller);
  // A tiny block (well under both the FFT window and DiracAnalyzer's own
  // 1024-sample accumulation threshold) isolates the loudness path from
  // the FFT/DirAC ones - this event carries no FFT/DirAC payload at all.
  thread.configure(3000, 256);

  int frames = 4;
  SampleData master(2, frames);
  master.zero();

  SampleData raw_bus(4, frames); // order-1 ambisonic - 4 regular channels (already even)
  for (int c = 0; c < 4; c++) {
    auto data = raw_bus.getChannelData(c);
    for (int i = 0; i < frames; i++) data[i] = static_cast<float>(c + 1) * 0.5f; // distinct constant per channel
  }
  SampleData aux_a(1, frames);
  for (int i = 0; i < frames; i++) aux_a.getChannelData(0)[i] = 0.25f;
  SampleData aux_b(1, frames);
  for (int i = 0; i < frames; i++) aux_b.getChannelData(0)[i] = 0.75f;

  AudioBlockEvent ev(std::move(master), std::move(raw_bus), std::move(aux_a), std::move(aux_b));
  thread.handleAudioBlockEvent(ev);

  auto result_event = controller.getUIEventQueue().pop();
  auto * result = dynamic_cast<VisualizationResultEvent *>(result_event.get());
  CHECK(result != nullptr);
  if (!result) return;

  // SampleData::calculateLoudness() is sqrt(sum of squares), not divided
  // by frame count - a constant channel of value c over `frames` samples
  // gives c*sqrt(frames).
  auto & levels = result->getChannelLoudness();
  CHECK(levels.size() == 6); // 4 regular (no padding needed) + auxA + auxB
  for (int c = 0; c < 4; c++) {
    float expected = static_cast<float>(c + 1) * 0.5f * sqrtf(static_cast<float>(frames));
    CHECK_NEAR(levels[static_cast<size_t>(c)], expected, 0.001f);
  }
  CHECK_NEAR(levels[4], 0.25f * sqrtf(static_cast<float>(frames)), 0.001f);
  CHECK_NEAR(levels[5], 0.75f * sqrtf(static_cast<float>(frames)), 0.001f);

  CHECK(result->getMeterLabel() == "M4A");
}

TEST(visualization_thread_channel_loudness_pads_odd_regular_count_before_aux) {
  Controller controller{ChannelConfiguration()};
  VisualizationThread thread(&controller);
  thread.configure(3000, 256);

  int frames = 2;
  SampleData master(2, frames);
  master.zero();

  SampleData raw_bus(9, frames); // order-2 ambisonic - 9 regular channels (odd)
  for (int c = 0; c < 9; c++) {
    auto data = raw_bus.getChannelData(c);
    for (int i = 0; i < frames; i++) data[i] = 1.0f;
  }
  SampleData aux_a(1, frames);
  aux_a.zero();
  SampleData aux_b(1, frames);
  aux_b.zero();

  AudioBlockEvent ev(std::move(master), std::move(raw_bus), std::move(aux_a), std::move(aux_b));
  thread.handleAudioBlockEvent(ev);

  auto result_event = controller.getUIEventQueue().pop();
  auto * result = dynamic_cast<VisualizationResultEvent *>(result_event.get());
  CHECK(result != nullptr);
  if (!result) return;

  // 9 regular channels, padded to 10 with a trailing silent slot (the
  // braille meter packs 2 samples/column, so AuxA/AuxB must start at an
  // even index), then auxA and auxB.
  auto & levels = result->getChannelLoudness();
  CHECK(levels.size() == 12);
  CHECK_NEAR(levels[9], 0.0f, 0.0001f); // the padding slot
  CHECK(result->getMeterLabel() == "M1-9 A");
}

TEST(visualization_thread_terminate_sentinel_produces_no_result) {
  // An empty SampleData (the sentinel AudioBlockEvent::run() watches for,
  // see AudioBlockEvent.h) must not be treated as real audio data - no
  // VisualizationResultEvent should come out of it.
  Controller controller{ChannelConfiguration()};
  VisualizationThread thread(&controller);
  thread.configure(3000, 256);

  AudioBlockEvent terminate_ev{SampleData(), SampleData(), SampleData(), SampleData()};
  thread.handleAudioBlockEvent(terminate_ev);

  CHECK(!controller.getUIEventQueue().hasEvents());
}
