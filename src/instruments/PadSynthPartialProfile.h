#ifndef _PADSYNTHPARTIALPROFILE_H_
#define _PADSYNTHPARTIALPROFILE_H_

#include <array>
#include <cstdint>
#include <vector>

// The PADsynth partial profile (docs/padsynth.md's "Partial profile
// p[0…511]") and partial-position g(h) formulas, clean-room-implemented
// from ZynAddSubFX's own public PADsynth algorithm description - see
// OscillatorShapingChain.h's own doc comment for the same GPL-source
// boundary this stays inside of.
namespace PadSynthProfile {

constexpr int kProfileSize = 512;

enum class ProfileType { Gaussian = 0, Rectangular = 1 };

struct ProfileParams {
  ProfileType type = ProfileType::Gaussian;
  float beta = 1.0f;
  bool autoscale = true;
  // The profile's own width scale - already the derived multiplier
  // buildProfile() applies directly (not ZynAddSubFX's own raw 0-127
  // HARMONIC_PROFILE `width` knob). Every preset observed in the example
  // data leaves the knob at its neutral/full-width setting (127), so this
  // defaults to that setting's own derived value - see docs/padsynth.md.
  float width_scale = 1.01346779f;
};

// Builds p[0..511]: supersamples 16 points per bin, averages into the bin,
// clips negative values to 0, and normalizes the maximum to 1.
std::array<float, kProfileSize> buildProfile(const ProfileParams & params);

// The "profile width correction" alpha - either the fixed 0.5 (autoscale
// off) or accumulated from p[]'s own tail energy (autoscale on) - see
// docs/padsynth.md's own accumulation rule.
float computeProfileAlpha(const std::array<float, kProfileSize> & profile, bool autoscale);

// Partial position g(h), h >= 1. Harmonic is the plain harmonic series;
// Stretch is ZynAddSubFX's own stretch/position formula - "the importer
// must reject any other value" is enforced by throwing
// std::invalid_argument for any other `type`.
enum class PositionType { Harmonic = 0, Stretch = 6 };

struct PositionParams {
  PositionType type = PositionType::Harmonic;
  // Stretch only - already-derived quantities (not ZynAddSubFX's own raw
  // stored bytes P1/P2/P3, 0-255), see partialPosition()'s own formula:
  //   stretch_strength  = 10^(-3 * (1 - P1/255))
  //   stretch_curvature = P2/255
  //   position_mix      = 1 - P3/255 (0 = snapped to the nearest harmonic,
  //                                    1 = the fully continuous position)
  float stretch_strength = 0.0f;
  float stretch_curvature = 0.0f;
  float position_mix = 0.0f;
};

float partialPosition(int h, const PositionParams & params);

// Places one partial's own profile-shaped energy into `amplitude_spectrum`
// (size `spectrum_size`, DC at index 0) - docs/padsynth.md's own "Place
// the profile" step, the two energy-preserving branches (nearest-neighbour
// stretch for a window wider than the profile itself, interpolated
// accumulation otherwise) kept as separate, deliberate code paths rather
// than unified into one - both exist specifically to keep a partial's own
// total energy independent of its window width. `bandwidth_cents` is c;
// `alpha` is the profile's own width correction - the real window is
// c/alpha wide, not c alone (see docs/padsynth.md).
void placePartial(std::vector<float> & amplitude_spectrum, float amplitude, float partial_frequency_hz,
                   float bandwidth_cents, float alpha, const std::array<float, kProfileSize> & profile,
                   float sample_rate);

} // namespace PadSynthProfile

#endif
