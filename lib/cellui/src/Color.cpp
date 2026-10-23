#include <cellui/Color.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace cellui {

namespace {

struct Mat {
  float m[9];
};

inline void apply(const Mat & mat, const float in[3], float out[3]) {
  out[0] = mat.m[0] * in[0] + mat.m[1] * in[1] + mat.m[2] * in[2];
  out[1] = mat.m[3] * in[0] + mat.m[4] * in[1] + mat.m[5] * in[2];
  out[2] = mat.m[6] * in[0] + mat.m[7] * in[1] + mat.m[8] * in[2];
}

// 3x3 helpers in double, for building the matrices from the primaries.
struct M3 {
  double v[9];
  double & operator[](int i) { return v[i]; }
  const double & operator[](int i) const { return v[i]; }
};

M3 multiply(const M3 & a, const M3 & b) {
  M3 r{{}};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      for (int k = 0; k < 3; k++) r[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
  return r;
}

M3 invert(const M3 & m) {
  double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
  double inv = 1.0 / det;
  return M3{{(m[4] * m[8] - m[5] * m[7]) * inv, (m[2] * m[7] - m[1] * m[8]) * inv, (m[1] * m[5] - m[2] * m[4]) * inv,
             (m[5] * m[6] - m[3] * m[8]) * inv, (m[0] * m[8] - m[2] * m[6]) * inv, (m[2] * m[3] - m[0] * m[5]) * inv,
             (m[3] * m[7] - m[4] * m[6]) * inv, (m[1] * m[6] - m[0] * m[7]) * inv, (m[0] * m[4] - m[1] * m[3]) * inv}};
}

// Linear RGB -> XYZ for primaries (x, y) and white point (x, y).
M3 rgbToXyz(const double (&p)[3][2], const double (&w)[2]) {
  M3 prim{{}};
  for (int i = 0; i < 3; i++) {
    prim[0 * 3 + i] = p[i][0] / p[i][1];
    prim[1 * 3 + i] = 1.0;
    prim[2 * 3 + i] = (1.0 - p[i][0] - p[i][1]) / p[i][1];
  }
  double white[3] = {w[0] / w[1], 1.0, (1.0 - w[0] - w[1]) / w[1]};
  M3 inv = invert(prim);
  double s[3];
  for (int i = 0; i < 3; i++) s[i] = inv[i * 3 + 0] * white[0] + inv[i * 3 + 1] * white[1] + inv[i * 3 + 2] * white[2];
  M3 r{{}};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) r[i * 3 + j] = prim[i * 3 + j] * s[j];
  return r;
}

Mat toMat(const M3 & m) {
  Mat r;
  for (int i = 0; i < 9; i++) r.m[i] = static_cast<float>(m[i]);
  return r;
}

float srgbDecode(float x) {
  float a = std::fabs(x);
  float v = a <= 0.04045f ? a / 12.92f : std::pow((a + 0.055f) / 1.055f, 2.4f);
  return x < 0 ? -v : v;
}

float srgbEncode(float x) {
  float a = std::fabs(x);
  float v = a <= 0.0031308f ? 12.92f * a : 1.055f * std::pow(a, 1.0f / 2.4f) - 0.055f;
  return x < 0 ? -v : v;
}

// BT.2020's transfer function, as CSS Color 4 gives it.
float rec2020Decode(float x) {
  constexpr float alpha = 1.09929682680944f, beta = 0.018053968510807f;
  float a = std::fabs(x);
  float v = a < beta * 4.5f ? a / 4.5f : std::pow((a + alpha - 1.0f) / alpha, 1.0f / 0.45f);
  return x < 0 ? -v : v;
}

constexpr int kLut = 4096;

struct Tables {
  float decode8[256];
  // Linear 709 light at which the sRGB encoding rounds up to level k + 1.
  float threshold[255];
  // The level at i / kLut (a start for the search among the thresholds), and
  // the transfer functions sampled over 0..1.
  uint8_t level_at[kLut + 1];
  float encode_lut[kLut + 2], decode_lut[kLut + 2];
  Mat m709to2020, m2020to709, p3to2020;

  Tables() {
    const double d65[2] = {0.3127, 0.3290};
    const double p709[3][2] = {{0.640, 0.330}, {0.300, 0.600}, {0.150, 0.060}};
    const double pp3[3][2] = {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}};
    const double p2020[3][2] = {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}};
    M3 x709 = rgbToXyz(p709, d65), xp3 = rgbToXyz(pp3, d65), x2020 = rgbToXyz(p2020, d65);
    M3 xyzTo2020 = invert(x2020);
    m709to2020 = toMat(multiply(xyzTo2020, x709));
    m2020to709 = toMat(multiply(invert(x709), x2020));
    p3to2020 = toMat(multiply(xyzTo2020, xp3));
    for (int i = 0; i < 256; i++) decode8[i] = srgbDecode(static_cast<float>(i) / 255.0f);
    for (int i = 0; i < 255; i++) threshold[i] = srgbDecode((static_cast<float>(i) + 0.5f) / 255.0f);
    for (int i = 0; i <= kLut; i++) {
      float x = static_cast<float>(i) / kLut;
      int level = 0;
      while (level < 255 && threshold[level] <= x) level++;
      level_at[i] = static_cast<uint8_t>(level);
      encode_lut[i] = srgbEncode(x);
      decode_lut[i] = srgbDecode(x);
    }
    encode_lut[kLut + 1] = encode_lut[kLut];
    decode_lut[kLut + 1] = decode_lut[kLut];
  }
};

const Tables & tables() {
  static const Tables t;
  return t;
}

// The transfer functions through the tables (their error is far below one
// 8-bit level), outside 0..1 the exact ones.
inline float lutLookup(const float * lut, float a) {
  float p = a * kLut;
  int i = static_cast<int>(p);
  return lut[i] + (lut[i + 1] - lut[i]) * (p - static_cast<float>(i));
}

inline float encodeFast(const Tables & t, float x) {
  float a = std::fabs(x);
  float v = a < 1.0f ? lutLookup(t.encode_lut, a) : srgbEncode(a);
  return x < 0 ? -v : v;
}

inline float decodeFast(const Tables & t, float x) {
  float a = std::fabs(x);
  float v = a < 1.0f ? lutLookup(t.decode_lut, a) : srgbDecode(a);
  return x < 0 ? -v : v;
}

// OKLab on linear Rec.709 light.
void toOklab(const float rgb[3], float lab[3]) {
  float l = 0.4122214708f * rgb[0] + 0.5363325363f * rgb[1] + 0.0514459929f * rgb[2];
  float m = 0.2119034982f * rgb[0] + 0.6806995451f * rgb[1] + 0.1073969566f * rgb[2];
  float s = 0.0883024619f * rgb[0] + 0.2817188376f * rgb[1] + 0.6299787005f * rgb[2];
  float l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);
  lab[0] = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
  lab[1] = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
  lab[2] = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
}

void fromOklab(const float lab[3], float rgb[3]) {
  float l_ = lab[0] + 0.3963377774f * lab[1] + 0.2158037573f * lab[2];
  float m_ = lab[0] - 0.1055613458f * lab[1] - 0.0638541728f * lab[2];
  float s_ = lab[0] - 0.0894841775f * lab[1] - 1.2914855480f * lab[2];
  float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
  rgb[0] = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
  rgb[1] = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
  rgb[2] = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

constexpr float kGamutSlack = 1e-4f;

// Pulls linear 709 `v` into the gamut `to709` describes (all of the target's
// components at least 0): chroma reduced at the colour's own OKLab lightness
// and hue. `target` is null for 709 itself.
void reduceChroma(float v[3], const Mat * target) {
  auto minOf = [&](const float c709[3]) {
    if (!target) return std::min(c709[0], std::min(c709[1], c709[2]));
    float t[3];
    apply(*target, c709, t);
    return std::min(t[0], std::min(t[1], t[2]));
  };
  if (minOf(v) >= -kGamutSlack * 0.1f) return;
  float lab[3];
  toOklab(v, lab);
  if (!(lab[0] > 0.0f)) {
    v[0] = v[1] = v[2] = 0.0f;
    return;
  }
  float lo = 0.0f, hi = 1.0f;
  for (int i = 0; i < 20; i++) {
    float mid = 0.5f * (lo + hi);
    float scaled[3] = {lab[0], lab[1] * mid, lab[2] * mid}, rgb[3];
    fromOklab(scaled, rgb);
    if (minOf(rgb) >= 0.0f) lo = mid;
    else hi = mid;
  }
  float scaled[3] = {lab[0], lab[1] * lo, lab[2] * lo};
  fromOklab(scaled, v);
  for (int i = 0; i < 3; i++) v[i] = std::fmax(0.0f, v[i]);
}

inline float finite(float x) { return std::isfinite(x) ? x : 0.0f; }

}  // namespace

Color::Color(int red, int green, int blue) {
  const Tables & t = tables();
  float v[3] = {t.decode8[std::clamp(red, 0, 255)], t.decode8[std::clamp(green, 0, 255)], t.decode8[std::clamp(blue, 0, 255)]}, o[3];
  apply(t.m709to2020, v, o);
  r_ = std::fmax(0.0f, o[0]);
  g_ = std::fmax(0.0f, o[1]);
  b_ = std::fmax(0.0f, o[2]);
}

Color Color::linear(float r, float g, float b) {
  r = finite(r);
  g = finite(g);
  b = finite(b);
  if (r >= 0.0f && g >= 0.0f && b >= 0.0f) return Color(r, g, b, 0);
  const Tables & t = tables();
  float v2020[3] = {r, g, b}, v709[3];
  apply(t.m2020to709, v2020, v709);
  reduceChroma(v709, &t.m709to2020);
  float o[3];
  apply(t.m709to2020, v709, o);
  return Color(std::fmax(0.0f, o[0]), std::fmax(0.0f, o[1]), std::fmax(0.0f, o[2]), 0);
}

Color Color::fromLinearSrgb(float r, float g, float b) {
  float v[3] = {finite(r), finite(g), finite(b)}, o[3];
  apply(tables().m709to2020, v, o);
  return linear(o[0], o[1], o[2]);
}

Color Color::srgb(float r, float g, float b) {
  float v[3] = {srgbDecode(finite(r)), srgbDecode(finite(g)), srgbDecode(finite(b))}, o[3];
  apply(tables().m709to2020, v, o);
  return linear(o[0], o[1], o[2]);
}

Color Color::displayP3(float r, float g, float b) {
  float v[3] = {srgbDecode(finite(r)), srgbDecode(finite(g)), srgbDecode(finite(b))}, o[3];
  apply(tables().p3to2020, v, o);
  return linear(o[0], o[1], o[2]);
}

Color Color::rec2020(float r, float g, float b) {
  return linear(rec2020Decode(finite(r)), rec2020Decode(finite(g)), rec2020Decode(finite(b)));
}

Color Color::fromHSL(float h, float s, float l) {
  h = std::fmod(h, 360.0f);
  if (h < 0.0f) h += 360.0f;
  float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
  float hp = h / 60.0f;
  float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
  float r1 = 0, g1 = 0, b1 = 0;
  if (hp < 1.0f) { r1 = c; g1 = x; b1 = 0; }
  else if (hp < 2.0f) { r1 = x; g1 = c; b1 = 0; }
  else if (hp < 3.0f) { r1 = 0; g1 = c; b1 = x; }
  else if (hp < 4.0f) { r1 = 0; g1 = x; b1 = c; }
  else if (hp < 5.0f) { r1 = x; g1 = 0; b1 = c; }
  else { r1 = c; g1 = 0; b1 = x; }
  float m = l - c / 2.0f;
  return Color(static_cast<int>((r1 + m) * 255.0f + 0.5f),
               static_cast<int>((g1 + m) * 255.0f + 0.5f),
               static_cast<int>((b1 + m) * 255.0f + 0.5f));
}

Color Color::fromOklch(float lightness, float chroma, float hue) {
  float rad = hue * 3.14159265f / 180.0f;
  float lab[3] = {std::fmax(0.0f, finite(lightness)), finite(chroma) * std::cos(rad), finite(chroma) * std::sin(rad)};
  float v709[3], o[3];
  fromOklab(lab, v709);
  apply(tables().m709to2020, v709, o);
  return linear(o[0], o[1], o[2]);
}

Color Color::fromOklchClipped(float lightness, float chroma, float hue) {
  float rad = hue * 3.14159265f / 180.0f;
  float lab[3] = {finite(lightness), finite(chroma) * std::cos(rad), finite(chroma) * std::sin(rad)}, rgb[3];
  fromOklab(lab, rgb);
  return Color(linearToSrgb8(rgb[0]), linearToSrgb8(rgb[1]), linearToSrgb8(rgb[2]));
}

Color Color::operator*(float gain) const {
  gain = std::fmax(0.0f, finite(gain));
  return Color(r_ * gain, g_ * gain, b_ * gain, 0);
}

Color Color::mix(const Color & other, float t, MixSpace space) const {
  t = std::clamp(finite(t), 0.0f, 1.0f);
  if (t == 0.0f) return *this;
  if (t == 1.0f) return other;
  if (space == MixSpace::LINEAR)
    return Color(r_ + (other.r_ - r_) * t, g_ + (other.g_ - g_) * t, b_ + (other.b_ - b_) * t, 0);
  const Tables & tb = tables();
  float a2020[3] = {r_, g_, b_}, b2020[3] = {other.r_, other.g_, other.b_}, a[3], b[3], o[3], out[3];
  apply(tb.m2020to709, a2020, a);
  apply(tb.m2020to709, b2020, b);
  if (space == MixSpace::SRGB) {
    for (int i = 0; i < 3; i++) {
      float ea = encodeFast(tb, a[i]), eb = encodeFast(tb, b[i]);
      o[i] = decodeFast(tb, ea + (eb - ea) * t);
    }
  } else {
    float la[3], lb[3], lm[3];
    toOklab(a, la);
    toOklab(b, lb);
    for (int i = 0; i < 3; i++) lm[i] = la[i] + (lb[i] - la[i]) * t;
    fromOklab(lm, o);
  }
  apply(tb.m709to2020, o, out);
  return linear(out[0], out[1], out[2]);
}

std::array<uint8_t, 3> Color::toSrgb8() const {
  const Tables & t = tables();
  float c[3] = {r_, g_, b_}, v[3];
  apply(t.m2020to709, c, v);
  if (std::fmin(v[0], std::fmin(v[1], v[2])) < 0.0f) reduceChroma(v, nullptr);
  float mx = std::fmax(v[0], std::fmax(v[1], v[2]));
  if (mx > 1.0f) {
    float k = 1.0f / mx;
    v[0] *= k;
    v[1] *= k;
    v[2] *= k;
  }
  return {linearToSrgb8(v[0]), linearToSrgb8(v[1]), linearToSrgb8(v[2])};
}

std::array<float, 3> Color::linearSrgb() const {
  float c[3] = {r_, g_, b_}, v[3];
  apply(tables().m2020to709, c, v);
  return {v[0], v[1], v[2]};
}

float Color::srgb8ToLinear(int level) { return tables().decode8[std::clamp(level, 0, 255)]; }

uint8_t Color::linearToSrgb8(float linear) {
  const Tables & t = tables();
  float x = std::clamp(finite(linear), 0.0f, 1.0f);
  int level = t.level_at[static_cast<int>(x * kLut)];
  while (level < 255 && t.threshold[level] <= x) level++;
  while (level > 0 && t.threshold[level - 1] > x) level--;
  return static_cast<uint8_t>(level);
}

float Color::hslHue() const {
  auto rgb = toSrgb8();
  float r = rgb[0] / 255.0f, g = rgb[1] / 255.0f, b = rgb[2] / 255.0f;
  float mx = std::fmax(r, std::fmax(g, b)), mn = std::fmin(r, std::fmin(g, b));
  float d = mx - mn;
  if (d <= 0.0f) return 0.0f;
  float h = mx == r ? std::fmod((g - b) / d, 6.0f) : mx == g ? (b - r) / d + 2.0f : (r - g) / d + 4.0f;
  h *= 60.0f;
  return h < 0.0f ? h + 360.0f : h;
}

size_t Color::hash() const {
  uint32_t bits[3];
  std::memcpy(&bits[0], &r_, 4);
  std::memcpy(&bits[1], &g_, 4);
  std::memcpy(&bits[2], &b_, 4);
  size_t h = 1469598103934665603ull;
  for (uint32_t b : bits) h = (h ^ b) * 1099511628211ull;
  return h;
}

namespace {

// A non-hex character counts as 0 rather than rejecting the whole value.
int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

bool startsWith(std::string_view s, std::string_view prefix) {
  if (s.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); i++)
    if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
  return true;
}

// The arguments of "name( ... )" split on spaces, commas and a slash (an
// alpha is ignored).
std::vector<std::string> arguments(std::string_view s) {
  size_t open = s.find('(');
  size_t close = s.rfind(')');
  std::vector<std::string> out;
  if (open == std::string_view::npos) return out;
  s = s.substr(open + 1, (close == std::string_view::npos || close < open ? s.size() : close) - open - 1);
  std::string cur;
  for (char c : s) {
    if (c == ' ' || c == ',' || c == '/' || c == '\t') {
      if (c == '/') {
        if (!cur.empty()) out.push_back(cur);
        return out;
      }
      if (!cur.empty()) out.push_back(cur), cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

// A number, or a percentage of `percent_of`; "none" is 0.
float number(const std::string & tok, float percent_of) {
  if (tok == "none") return 0.0f;
  char * end = nullptr;
  float v = std::strtof(tok.c_str(), &end);
  if (end && *end == '%') return v / 100.0f * percent_of;
  return v;
}

}  // namespace

void Color::setValue(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  if (startsWith(s, "oklch(")) {
    auto a = arguments(s);
    if (a.size() >= 3) *this = fromOklch(number(a[0], 1.0f), number(a[1], 0.4f), number(a[2], 360.0f));
    else *this = Color();
    return;
  }
  if (startsWith(s, "color(")) {
    auto a = arguments(s);
    *this = Color();
    if (a.size() < 4) return;
    std::string space = a[0];
    for (auto & ch : space) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    float x = number(a[1], 1.0f), y = number(a[2], 1.0f), z = number(a[3], 1.0f);
    if (space == "srgb") *this = srgb(x, y, z);
    else if (space == "srgb-linear") {
      float v[3] = {x, y, z}, o[3];
      apply(tables().m709to2020, v, o);
      *this = linear(o[0], o[1], o[2]);
    }
    else if (space == "display-p3") *this = displayP3(x, y, z);
    else if (space == "rec2020") *this = rec2020(x, y, z);
    return;
  }
  if (!s.empty() && s[0] == '#') s.remove_prefix(1);
  int r = 0, g = 0, b = 0;
  if (s.size() >= 6) {
    r = hexDigit(s[0]) * 16 + hexDigit(s[1]);
    g = hexDigit(s[2]) * 16 + hexDigit(s[3]);
    b = hexDigit(s[4]) * 16 + hexDigit(s[5]);
  } else if (s.size() >= 3) {
    // #rgb is #rrggbb with each digit doubled
    r = hexDigit(s[0]) * 17;
    g = hexDigit(s[1]) * 17;
    b = hexDigit(s[2]) * 17;
  }
  *this = Color(r, g, b);
}

}  // namespace cellui
