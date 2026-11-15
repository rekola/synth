#include "JustIntonation.h"
#include "Note.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace just_intonation {

bool ScaleCorrections::nearerSecond(size_t step, double current) const {
  if (!second_present[step]) return false;
  return std::fabs(second_cents[step] - current) < std::fabs(cents[step] - current) - 1e-9;
}

std::optional<double> ScaleCorrections::forNote(int note_value, double current) const {
  if (edo <= 0) return std::nullopt;
  auto step = static_cast<size_t>((((note_value - key) % edo) + edo) % edo);
  if (!present[step]) return std::nullopt;
  return nearerSecond(step, current) ? second_cents[step] : cents[step];
}

std::optional<ScaleCorrections::NoteRatio> ScaleCorrections::ratioForNote(int note_value, double current) const {
  if (edo <= 0) return std::nullopt;
  const int from_key = note_value - key;
  const int octaves = from_key >= 0 ? from_key / edo : -((-from_key + edo - 1) / edo);
  auto step = static_cast<size_t>(from_key - octaves * edo);
  if (!present[step]) return std::nullopt;
  return NoteRatio{nearerSecond(step, current) ? second_ratios[step] : ratios[step], octaves};
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
  // in the scale, else the one closer to it. In a scale with groups (the
  // extended diamond) that is decided per group, and a key a group holds a
  // different note on than the first carries that note as its second.
  bool first_group = true;
  for (auto group : just->groups()) {
    std::array<bool, ScaleCorrections::kMaxSteps> held{};
    std::array<bool, ScaleCorrections::kMaxSteps> in_scale{};
    std::array<double, ScaleCorrections::kMaxSteps> error{};
    std::array<double, ScaleCorrections::kMaxSteps> cents{};
    std::array<Ratio, ScaleCorrections::kMaxSteps> ratios{};
    for (const auto & note : just->gamut) {
      int step = ((note.ratio.steps(edo) % edo) + edo) % edo;
      auto index = static_cast<size_t>(step);
      bool member = just->inGroup(note, group);
      double off = std::fabs(note.ratio.cents() - note.ratio.steps(edo) * step_cents);
      if (held[index]) {
        bool better = member != in_scale[index] ? member : off < error[index] - 1e-9;
        if (!better) continue;
      }
      cents[index] = note.ratio.cents() - note.ratio.steps(edo) * step_cents;
      held[index] = true;
      // A ratio whose nearest step is the octave stands for the step at 0.
      ratios[index] = note.ratio.steps(edo) >= edo ? note.ratio / 2 : note.ratio;
      in_scale[index] = member;
      error[index] = off;
    }
    for (size_t i = 0; i < ScaleCorrections::kMaxSteps; i++) {
      if (!held[i]) continue;
      if (first_group) {
        out.cents[i] = cents[i];
        out.present[i] = true;
        out.ratios[i] = ratios[i];
      } else if (out.present[i] && out.ratios[i] != ratios[i]) {
        out.second_cents[i] = cents[i];
        out.second_present[i] = true;
        out.second_ratios[i] = ratios[i];
      }
    }
    first_group = false;
  }
  return out;
}
}
