#include "JustIntonation.h"
#include "Note.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace just_intonation {

std::optional<int> ScaleCorrections::forNote(int note_value) const {
  if (edo <= 0) return std::nullopt;
  auto step = static_cast<size_t>((((note_value - key) % edo) + edo) % edo);
  if (!present[step]) return std::nullopt;
  return cents[step];
}

ScaleCorrections scaleCorrectionsFor(Tuning tuning, Scale scale, int key) {
  ScaleCorrections out;
  int edo = edoStepsFor(tuning);
  auto just = justScale(scale);
  if (edo <= 0 || edo > ScaleCorrections::kMaxSteps || !just) return out;
  out.edo = edo;
  out.key = ((std::max(key, 0) % edo) + edo) % edo;
  const double step_cents = 1200.0 / edo;

  // Each note goes to its nearest step. A step several share is held by the one
  // in the scale, else the one closer to it.
  std::array<bool, ScaleCorrections::kMaxSteps> in_scale{};
  std::array<double, ScaleCorrections::kMaxSteps> error{};
  for (const auto & note : just->gamut) {
    int step = ((note.ratio.steps(edo) % edo) + edo) % edo;
    auto index = static_cast<size_t>(step);
    bool member = just->contains(note);
    double off = std::fabs(note.ratio.cents() - note.ratio.steps(edo) * step_cents);
    if (out.present[index]) {
      bool better = member != in_scale[index] ? member : off < error[index] - 1e-9;
      if (!better) continue;
    }
    out.cents[index] = static_cast<int16_t>(std::lround(note.ratio.cents() - note.ratio.steps(edo) * step_cents));
    out.present[index] = true;
    in_scale[index] = member;
    error[index] = off;
  }
  return out;
}
}
