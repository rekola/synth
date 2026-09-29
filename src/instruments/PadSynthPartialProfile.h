#ifndef _IMPORTEDPADSYNTHPROFILE_H_
#define _IMPORTEDPADSYNTHPROFILE_H_

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
  // "width" (ZynAddSubFX's own HARMONIC_PROFILE `width` parameter, 0-127) -
  // not given directly by any imported preset's own derived-value table
  // (every preset observed in the example data leaves it at its neutral/
  // full-width setting), so this defaults to 127 (the value actually
  // observed in the raw extracted patch data) rather than being left
  // unparameterized - see docs/padsynth.md.
  int width = 127;
  bool autoscale = true;
};

// Builds p[0..511]: supersamples 16 points per bin, averages into the bin,
// clips negative values to 0, and normalizes the maximum to 1.
std::array<float, kProfileSize> buildProfile(const ProfileParams & params);

// The "profile width correction" alpha - either the fixed 0.5 (autoscale
// off) or accumulated from p[]'s own tail energy (autoscale on) - see
// docs/padsynth.md's own accumulation rule.
float computeProfileAlpha(const std::array<float, kProfileSize> & profile, bool autoscale);

// Partial position g(h), h >= 1. `type` 0 is the plain harmonic series;
// `type` 6 is ZynAddSubFX's own stretch/position formula, parameterized by
// three raw stored bytes (P1/P2/P3, 0-255) exactly as the imported preset
// data gives them - "the importer must reject any other value" is
// enforced by throwing std::invalid_argument for any other `type`.
struct PositionParams {
  int type = 0;
  int p1 = 0, p2 = 0, p3 = 0; // type 6 only
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
