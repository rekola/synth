#include "PadSynthPartialProfile.h"

#include "../util/MathUtils.h"

#include <cmath>
#include <stdexcept>

using namespace std;

namespace PadSynthProfile {

std::array<float, kProfileSize> buildProfile(const ProfileParams & params) {
  constexpr int kSupersample = 16;
  std::array<float, kProfileSize> profile{};

  float w = 150.0f / (static_cast<float>(params.width) + 22.0f);
  w = w * w;

  for (int bin = 0; bin < kProfileSize; bin++) {
    float sum = 0.0f;
    for (int s = 0; s < kSupersample; s++) {
      float x = (static_cast<float>(bin) * kSupersample + static_cast<float>(s))
        / static_cast<float>(kProfileSize * kSupersample);
      float u = (x - 0.5f) * w + 0.5f;
      float contribution = 0.0f;
      if (u >= 0.0f && u <= 1.0f) {
        float v = frac(u) * 2.0f - 1.0f;
        float gaussian = std::exp(-v * v * params.beta);
        if (params.type == ProfileType::Gaussian) {
          contribution = gaussian;
        } else if (params.type == ProfileType::Rectangular) {
          contribution = (gaussian >= 0.4f) ? 1.0f : 0.0f;
        } else {
          throw std::invalid_argument("buildProfile: unsupported profile type");
        }
      }
      sum += contribution;
    }
    profile[static_cast<size_t>(bin)] = sum / static_cast<float>(kSupersample);
  }

  float peak = 0.0f;
  for (float & v : profile) {
    if (v < 0.0f) v = 0.0f;
    peak = std::max(peak, v);
  }
  if (peak > 0.0f) {
    for (float & v : profile) v /= peak;
  }
  return profile;
}

float computeProfileAlpha(const std::array<float, kProfileSize> & profile, bool autoscale) {
  if (!autoscale) return 0.5f;

  float sum = 0.0f;
  int i = 0;
  for (; i < 254; i++) {
    float lo = profile[static_cast<size_t>(i)];
    float hi = profile[static_cast<size_t>(kProfileSize - 1 - i)];
    sum += lo * lo + hi * hi;
    if (sum >= 4.0f) break;
  }
  return 1.0f - 2.0f * static_cast<float>(i) / static_cast<float>(kProfileSize);
}

void placePartial(std::vector<float> & amplitude_spectrum, float amplitude, float partial_frequency_hz,
                   float bandwidth_cents, float alpha, const std::array<float, kProfileSize> & profile,
                   float sample_rate) {
  int S = static_cast<int>(amplitude_spectrum.size());
  float nyquist_half = sample_rate * 0.5f;
  float bandwidth_ratio = std::exp2(bandwidth_cents / 1200.0f) - 1.0f;

  int omega = static_cast<int>(std::floor(bandwidth_ratio * partial_frequency_hz / alpha / nyquist_half * static_cast<float>(S))) + 1;
  float center = partial_frequency_hz / nyquist_half * static_cast<float>(S);

  if (omega > kProfileSize) {
    float r = std::sqrt(static_cast<float>(kProfileSize) / static_cast<float>(omega));
    int base_bin = static_cast<int>(std::floor(center)) - omega / 2;
    for (int i = 0; i < omega; i++) {
      int bin = base_bin + i;
      if (bin < 0) continue;
      if (bin >= S) break;
      float r2 = r * r;
      int profile_idx = static_cast<int>(std::floor(static_cast<float>(i) * r2));
      if (profile_idx < 0) profile_idx = 0;
      if (profile_idx >= kProfileSize) profile_idx = kProfileSize - 1;
      amplitude_spectrum[static_cast<size_t>(bin)] += amplitude * profile[static_cast<size_t>(profile_idx)] * r;
    }
  } else {
    float r = std::sqrt(static_cast<float>(omega) / static_cast<float>(kProfileSize));
    for (int i = 0; i < kProfileSize; i++) {
      float t = (static_cast<float>(i) / static_cast<float>(kProfileSize) - 0.5f) * static_cast<float>(omega) + center;
      int j_prime = static_cast<int>(std::floor(t));
      float frac_t = t - static_cast<float>(j_prime);
      if (j_prime <= 0) continue;
      if (j_prime >= S - 1) break;
      float contribution = amplitude * profile[static_cast<size_t>(i)] * r;
      amplitude_spectrum[static_cast<size_t>(j_prime)] += contribution * (1.0f - frac_t);
      amplitude_spectrum[static_cast<size_t>(j_prime + 1)] += contribution * frac_t;
    }
  }
}

float partialPosition(int h, const PositionParams & params) {
  if (params.type == 0) return static_cast<float>(h);
  if (params.type != 6) throw std::invalid_argument("partialPosition: unsupported position type");

  float pi1 = std::pow(10.0f, -3.0f * (1.0f - static_cast<float>(params.p1) / 255.0f));
  float pi2 = static_cast<float>(params.p2) / 255.0f;
  float h0 = static_cast<float>(h - 1);
  float e = (2.0f * pi2) * (2.0f * pi2) + 0.1f;
  float r = h0 * std::pow(1.0f + pi1 * std::pow(0.8f * h0, e), e) + 1.0f;
  float rho = std::floor(r + 0.5f);
  return rho + (1.0f - static_cast<float>(params.p3) / 255.0f) * (r - rho);
}

} // namespace PadSynthProfile
