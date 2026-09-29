#ifndef _PARTIALPOSITION_H_
#define _PARTIALPOSITION_H_

#include <cmath>
#include <vector>

// A preset-level description of g(h): where partial h actually sits,
// expressed as a ratio to the fundamental (g(1) == 1 is the ordinary
// case). Not XML-exposed (like PadSynthFormant/harmonic_amplitudes,
// ParameterSource has no array type) - a curated, per-preset shape, not a
// user-authorable song parameter.
struct PartialPositionSpec {
  enum class Kind {
    Harmonic,          // g(h) == h - the ordinary harmonic series.
    IntegerList,       // g(h) == explicit_positions[h-1] (falls back to h
                        // past the list's own end).
    FractionalStretch, // g(h) == h * sqrt(1 + stretch_b * h^2) - the same
                        // stiff-string formula SinusoidBank.cpp's own
                        // additivePartialRatio() already uses, reused here
                        // rather than inventing a second one; see
                        // stretchCoefficientForAnchor() below for how a
                        // preset picks stretch_b from one measured anchor
                        // point instead of guessing it directly.
  };

  Kind kind = Kind::Harmonic;
  std::vector<int> explicit_positions; // IntegerList only, index 0 = h = 1
  float stretch_b = 0.0f;              // FractionalStretch only
};

inline float partialPosition(int h, const PartialPositionSpec & spec) {
  switch (spec.kind) {
    case PartialPositionSpec::Kind::IntegerList:
      if (h >= 1 && static_cast<size_t>(h) <= spec.explicit_positions.size()) {
        return static_cast<float>(spec.explicit_positions[static_cast<size_t>(h - 1)]);
      }
      return static_cast<float>(h);
    case PartialPositionSpec::Kind::FractionalStretch:
      return static_cast<float>(h) * std::sqrt(1.0f + spec.stretch_b * static_cast<float>(h) * static_cast<float>(h));
    case PartialPositionSpec::Kind::Harmonic:
    default:
      return static_cast<float>(h);
  }
}

// Solves stretch_b so that partial `anchor_h`'s own g(h) lands exactly at
// `anchor_position` (e.g. "partial 10 at 10.04") - a real measured patch
// datum is given as one stretched point, not a raw stiffness coefficient,
// so this is the natural way to author a FractionalStretch preset:
// g(anchor_h) = anchor_h*sqrt(1+B*anchor_h^2) == anchor_position, solved
// for B.
inline float stretchCoefficientForAnchor(int anchor_h, float anchor_position) {
  if (anchor_h < 1) return 0.0f;
  float h = static_cast<float>(anchor_h);
  float ratio = anchor_position / h;
  return (ratio * ratio - 1.0f) / (h * h);
}

#endif
