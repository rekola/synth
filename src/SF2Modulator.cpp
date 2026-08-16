#include "SF2Modulator.h"

#include <cmath>

namespace SF2Mod {

  Source parseModSource(uint16_t raw) {
    Source s;
    s.index = static_cast<uint8_t>(raw & 0x7F);
    s.isMidiCC = (raw & 0x80) != 0;
    s.decreasing = (raw & 0x100) != 0;
    s.bipolar = (raw & 0x200) != 0;
    s.curve = static_cast<CurveType>((raw >> 10) & 0x3F);
    return s;
  }

  static float applyCurveShape(float x, CurveType curve) {
    switch (curve) {
    case CurveType::Linear:
      return x;
    case CurveType::Switch:
      return x < 0.5f ? 0.0f : 1.0f;
    case CurveType::Concave:
      // Audio-taper "concave": slow near 0, fast near 1. f(0)=0, f(1)=1.
      return 1.0f - log10f(1.0f + 9.0f * (1.0f - x));
    case CurveType::Convex:
      // Mirror of Concave: fast near 0, slow near 1. f(0)=0, f(1)=1.
      return log10f(1.0f + 9.0f * x);
    }
    return x;
  }

  float applySourceCurve(float x01, const Source & source) {
    float x = source.decreasing ? (1.0f - x01) : x01;
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    float shaped = applyCurveShape(x, source.curve);
    return source.bipolar ? (2.0f * shaped - 1.0f) : shaped;
  }

  bool sameIdentity(const Connection & a, const Connection & b) {
    return a.src == b.src && a.dest == b.dest && a.amtSrc == b.amtSrc && a.trans == b.trans;
  }

  std::vector<Connection> mergeModulators(const std::vector<Connection> & base, const std::vector<Connection> & overrides) {
    std::vector<Connection> result;
    result.reserve(base.size() + overrides.size());
    for (const auto & b : base) {
      bool replaced = false;
      for (const auto & o : overrides) {
        if (sameIdentity(b, o)) {
          replaced = true;
          break;
        }
      }
      if (!replaced) result.push_back(b);
    }
    for (const auto & o : overrides) result.push_back(o);
    return result;
  }

  bool isChannelPressureSourced(const Connection & c) {
    Source source = parseModSource(c.src);
    Source amtSource = parseModSource(c.amtSrc);
    return !source.isMidiCC && source.index == static_cast<uint8_t>(GeneralController::ChannelPressure) &&
           !amtSource.isMidiCC && amtSource.index == static_cast<uint8_t>(GeneralController::NoController);
  }

  float evaluateChannelPressureModulator(const Connection & c, float pressure01) {
    Source source = parseModSource(c.src);
    float value = applySourceCurve(pressure01, source) * static_cast<float>(c.amount);
    // sfModTransOper (SF2.01 8.2.1): 0 = Linear, 2 = Absolute Value.
    if (c.trans == 2) value = value < 0.0f ? -value : value;
    return value;
  }

}
