#include "VisualizationThread.h"
#include "../Controller.h"
#include "AudioBlockEvent.h"
#include "VisualizationResultEvent.h"

#include <array>
#include <poll.h>
#include <memory>

using namespace std;

void
VisualizationThread::handleAudioBlockEvent(AudioBlockEvent & ev) {
  if (ev.getMaster().empty()) {
    terminate_ = true;
    return;
  }

  // Always produced (unlike the FFT/DirAC payloads below, each gated
  // behind its own throttle) - the raw-channel volume meter tracks every
  // audio block, since that's the cadence a level meter needs to read as
  // live (see Player.cpp for why this scan runs here rather than on the
  // real-time audio thread).
  auto result = make_unique<VisualizationResultEvent>();

  // Raw, pre-mixdown per-channel loudness for the UI's volume meter - the
  // ambisonic bus (whatever regular channel count is active), then
  // always AuxA/AuxB last (see SongState::renderBlock()'s aux_a_sum_/
  // aux_b_sum_, and AudioBlockEvent.h for why they arrive as separate
  // fields from raw_bus).
  auto channel_loudness = ev.getRawBus().calculateLoudness();

  // Meter legend - each *character* lines up with one meter *column* (2
  // samples/braille-column), so the label reads as an actual legend for
  // the bars beneath it rather than just a compact tag: the "A" for
  // AuxA/AuxB is always placed at the exact column index where the aux
  // channels themselves start (padded with spaces to get there), never
  // just appended to the end of the text. There is no plain-stereo
  // config any more (ChannelConfiguration::STEREO was removed - every
  // config is MONO or AMBISONIC), so there's no "2 regular channels"
  // case to label here.
  //
  // "M" always marks where the Main (regular/ambisonic) channels start,
  // "A" where AuxA/AuxB start - never "A" for both meanings in the same
  // label. mono+aux (1 regular -> padded to 2 -> 1 col, then aux -> col
  // 1): "M" (mono) + "A" (aux, col 1) = "MA". Order-1 ambisonic (4
  // regular -> 2 cols, then aux -> col 2): "M4" (Main, 4 channels) + "A"
  // (col 2) = "M4A". Order-2 (9 regular, odd -> padded to 10 -> 5 cols,
  // then aux -> col 5): "M1-9" + " " (col 4) + "A" (col 5) = "M1-9 A".
  // Order-3 (16 regular, even -> 8 cols, then aux -> col 8): "M1-16" + 3
  // spaces (cols 5-7) + "A" (col 8) = "M1-16   A".
  switch (channel_loudness.size()) {
  case 1: result->setMeterLabel("MA"); break;
  case 4: result->setMeterLabel("M4A"); break;
  case 9: result->setMeterLabel("M1-9 A"); break;
  case 16: result->setMeterLabel("M1-16   A"); break;
  default: result->setMeterLabel(""); break;
  }

  // Pad to an even count before appending AuxA/AuxB - the braille meter
  // packs 2 samples per character cell, so the aux channels only land
  // together in the *same* cell (rather than the last regular channel
  // pairing with AuxA, leaving AuxB alone) when they start at an even
  // index. Order-2 ambisonic (9, odd) needs this; stereo (2) and order-1
  // ambisonic (4) are already even.
  if (channel_loudness.size() % 2 == 1) channel_loudness.push_back(0.0f);

  auto aux_a = ev.getAuxA().calculateLoudness();
  auto aux_b = ev.getAuxB().calculateLoudness();
  channel_loudness.insert(channel_loudness.end(), aux_a.begin(), aux_a.end());
  channel_loudness.insert(channel_loudness.end(), aux_b.begin(), aux_b.end());
  result->setChannelLoudness(std::move(channel_loudness));

  if (spectrum_.addData(ev.getMaster())) {
    spectrum_.reset();
    result->setFFT(spectrum_.calculateFFT());
  }

  dirac_->process(ev.getRawBus());
  // plans/dirac-heatmap-scope.md SS1: deliver the DirAC grid at roughly
  // every 3rd analysis frame (~28.7Hz), not every one (~86Hz) - terminal
  // redraw/color-mapping cost buys nothing above that. Diffed against a
  // saved frame count, not a fixed "did process() just fire" flag, so a
  // single process() call that internally advances several frames at once
  // (a large or backlogged AudioBlockEvent) still gets caught correctly.
  if (dirac_->getAnalysisFrameCount() - dirac_last_pushed_frame_ >= 3) {
    dirac_last_pushed_frame_ = dirac_->getAnalysisFrameCount();
    array<float, DiracAnalyzer::kNumBands> diffuse_energy {};
    for (int b = 0; b < DiracAnalyzer::kNumBands; b++) diffuse_energy[static_cast<size_t>(b)] = dirac_->getDiffuseEnergy(b);

    result->setDiracGrid(dirac_->getGrid(), diffuse_energy);
  }

  controller_->getUIEventQueue().push(move(result));
}

void
VisualizationThread::run() {
  auto & queue = controller_->getVisualizationQueue();

  pollfd descriptor;
  descriptor.fd = queue.getPollFd();
  descriptor.events = POLLIN;

  while (!terminate_) {
    if (poll(&descriptor, 1, 1000) > 0 && descriptor.revents) {
      auto event = queue.pop();
      handleEvent(*event);
      while (queue.hasEvents()) {
        auto event2 = queue.pop();
        handleEvent(*event2);
      }
    }
  }
}
