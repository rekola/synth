#include "VisualizationThread.h"
#include "Controller.h"
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

  unique_ptr<VisualizationResultEvent> result;

  if (spectrum_.addData(ev.getMaster())) {
    spectrum_.reset();
    result = make_unique<VisualizationResultEvent>();
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

    if (!result) result = make_unique<VisualizationResultEvent>();
    result->setDiracGrid(dirac_->getGrid(), diffuse_energy);
  }

  if (result) controller_->getUIEventQueue().push(move(result));
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
