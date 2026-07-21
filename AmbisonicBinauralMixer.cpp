#include "AmbisonicBinauralMixer.h"

#include <mysofa.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <fmt/core.h>

using namespace std;

namespace {

// Mirrors findDefaultSoundFont() in Controller.cpp: project-local override
// first, then well-known directories - including /usr/share/libmysofa,
// where Ubuntu's libmysofa1 package itself ships real SOFA files
// (default.sofa, MIT_KEMAR_normal_pinna.sofa - confirmed present on this
// system), so binaural decoding works out of the box on Ubuntu without
// needing to source a SOFA file separately.
string findDefaultSofaFile() {
  namespace fs = std::filesystem;
  error_code ec;

  for (auto & entry : fs::directory_iterator("data", ec)) {
    if (entry.path().extension() == ".sofa") return entry.path().string();
  }

  vector<fs::path> dirs;
  if (auto home = getenv("HOME")) {
    dirs.push_back(fs::path(home) / ".local/share/sofa");
  }
  dirs.push_back("/usr/share/libmysofa");
  dirs.push_back("/usr/share/sofa");

  const char * preferred[] = {
    "default.sofa",
    "MIT_KEMAR_normal_pinna.sofa",
  };
  for (auto name : preferred) {
    for (auto & dir : dirs) {
      auto p = dir / name;
      if (fs::is_regular_file(p, ec)) return p.string();
    }
  }

  fs::path best;
  uintmax_t best_size = 0;
  for (auto & dir : dirs) {
    for (auto & entry : fs::directory_iterator(dir, ec)) {
      if (entry.path().extension() != ".sofa") continue;
      auto size = fs::file_size(entry.path(), ec);
      if (!ec && size > best_size) {
	best_size = size;
	best = entry.path();
      }
    }
  }
  return best.string();
}

// Two speaker layouts, chosen by ambisonic order (see speakerDirectionsFor):
// order 1 (4 channels, W/Y/Z/X only) keeps the original 8-speaker cube - a
// 12-speaker icosahedron wouldn't buy anything decoding from just 4 basis
// functions, and an evenly-spaced cube is already a good fit for FOA
// content. Order 2 (9 channels) moves to a 12-speaker icosahedron, which is
// what actually exploits the 5 additional degree-2 basis functions for
// finer spatial resolution. Azimuth here is in this engine's convention
// (positive = right, see PanLaw.h) - converted to libmysofa/SOFA's
// convention (positive = left, measured from the +x/front axis) via a
// sign flip below, in the loop that builds each SpeakerFilter.

// Cube vertices: 4 azimuths x 2 elevations. +-35.264 degrees
// (atan(1/sqrt(2))) is the true cube-vertex angle.
constexpr float kCubeElevation = 35.264389682754654f;

// Icosahedron vertices: 2 poles + two 5-vertex rings at the true
// icosahedron-vertex elevation (atan(0.5), the angle between a
// pentagonal-ring vertex and the polar axis), the rings offset 36 degrees
// from each other.
constexpr float kIcosahedronElevation = 26.56505117707799f; // atan(0.5), degrees

struct SpeakerDirection { float azimuth, elevation; };

std::vector<SpeakerDirection> speakerDirectionsFor(int ambisonic_channels) {
  if (ambisonic_channels <= 4) {
    return {
      { 45.0f, kCubeElevation }, { 135.0f, kCubeElevation }, { -135.0f, kCubeElevation }, { -45.0f, kCubeElevation },
      { 45.0f, -kCubeElevation }, { 135.0f, -kCubeElevation }, { -135.0f, -kCubeElevation }, { -45.0f, -kCubeElevation },
    };
  }
  return {
    { 0.0f, 90.0f }, { 0.0f, -90.0f },
    { 0.0f, kIcosahedronElevation }, { 72.0f, kIcosahedronElevation }, { 144.0f, kIcosahedronElevation }, { 216.0f, kIcosahedronElevation }, { 288.0f, kIcosahedronElevation },
    { 36.0f, -kIcosahedronElevation }, { 108.0f, -kIcosahedronElevation }, { 180.0f, -kIcosahedronElevation }, { 252.0f, -kIcosahedronElevation }, { 324.0f, -kIcosahedronElevation },
  };
}

} // namespace

AmbisonicBinauralMixer::AmbisonicBinauralMixer(int ambisonic_channels, int outSampleRate)
  : Mixer(2, outSampleRate), ambisonic_channels_(ambisonic_channels), buffer_(static_cast<short>(ambisonic_channels), 0) {
  auto sofa_path = findDefaultSofaFile();
  if (sofa_path.empty()) return;

  int filterlength = 0, err = 0;
  easy_ = mysofa_open(sofa_path.c_str(), static_cast<float>(outSampleRate), &filterlength, &err);
  if (!easy_ || err != 0 || filterlength <= 0) {
    easy_ = nullptr;
    return;
  }

  fmt::print(stderr, "Using SOFA file {} for binaural decoding\n", sofa_path);

  int max_ir_and_delay = 0;
  for (auto & dir : speakerDirectionsFor(ambisonic_channels_)) {
    SpeakerFilter filter;
    filter.decode_gains = computeAmbisonicGains(SphericalPosition{ dir.azimuth, dir.elevation, 1.0f });

    constexpr float kDeg2Rad = static_cast<float>(M_PI) / 180.0f;
    float sofa_azimuth = -dir.azimuth * kDeg2Rad; // this engine: +az = right; SOFA: +az = left
    float el = dir.elevation * kDeg2Rad;
    float x = cosf(el) * cosf(sofa_azimuth);
    float y = cosf(el) * sinf(sofa_azimuth);
    float z = sinf(el);

    filter.left_ir.resize(static_cast<size_t>(filterlength));
    filter.right_ir.resize(static_cast<size_t>(filterlength));
    float delay_left = 0.0f, delay_right = 0.0f;
    mysofa_getfilter_float(easy_, x, y, z, filter.left_ir.data(), filter.right_ir.data(), &delay_left, &delay_right);
    filter.left_delay = static_cast<int>(lroundf(delay_left));
    filter.right_delay = static_cast<int>(lroundf(delay_right));
    if (filter.left_delay < 0) filter.left_delay = 0;
    if (filter.right_delay < 0) filter.right_delay = 0;

    int candidate = filterlength + std::max(filter.left_delay, filter.right_delay);
    if (candidate > max_ir_and_delay) max_ir_and_delay = candidate;

    speakers_.push_back(std::move(filter));
  }

  left_tail_.assign(static_cast<size_t>(max_ir_and_delay), 0.0f);
  right_tail_.assign(static_cast<size_t>(max_ir_and_delay), 0.0f);
  ready_ = true;
}

AmbisonicBinauralMixer::~AmbisonicBinauralMixer() {
  if (easy_) mysofa_close(easy_);
}

void
AmbisonicBinauralMixer::reset() {
  buffer_.zero();
}

void
AmbisonicBinauralMixer::accumulate(const SampleData & input) {
  if (buffer_.numberOfFrames() != input.numberOfFrames()) {
    buffer_ = SampleData(static_cast<short>(ambisonic_channels_), input.numberOfFrames());
    buffer_.zero();
  }
  // mixNamed() rather than mix() - `input` (a track's rendered output) may
  // carry SendA/SendB trailing its regular ambisonic channels (see
  // Mixer.h); buffer_ never marks them present (raw-count constructor) so
  // they're silently ignored here.
  buffer_.mixNamed(input);
}

SampleData
AmbisonicBinauralMixer::encode() {
  int frames = buffer_.numberOfFrames();
  size_t tail_len = left_tail_.size();
  size_t acc_size = static_cast<size_t>(frames) + tail_len;

  // Reused across calls (see the members' own comments) - resize() only
  // reallocates the first time a given block size grows past whatever was
  // previously reserved; once warmed up (block size is constant in
  // practice), this is allocation-free.
  left_acc_.resize(acc_size);
  right_acc_.resize(acc_size);
  speaker_signal_.resize(static_cast<size_t>(frames));

  for (size_t i = 0; i < tail_len; i++) {
    left_acc_[i] = left_tail_[i];
    right_acc_[i] = right_tail_[i];
  }
  std::fill(left_acc_.begin() + static_cast<long>(tail_len), left_acc_.end(), 0.0f);
  std::fill(right_acc_.begin() + static_cast<long>(tail_len), right_acc_.end(), 0.0f);

  // buffer_ is constructed via the raw-count constructor, so it never
  // marks any channel present - it always holds exactly ambisonic_channels_
  // regular channels (never any sends), gathered once per call.
  int regular = buffer_.numberOfChannels();
  float * channels[kAmbisonicChannelCount] = {};
  for (int c = 0; c < regular; c++) channels[c] = buffer_.getChannelData(c);

  for (auto & speaker : speakers_) {
    auto & g = speaker.decode_gains;
    for (int i = 0; i < frames; i++) {
      float s = 0.0f;
      for (int c = 0; c < regular; c++) s += g[static_cast<size_t>(c)] * channels[c][i];
      speaker_signal_[static_cast<size_t>(i)] = s;
    }

    auto ir_len = speaker.left_ir.size();
    for (int i = 0; i < frames; i++) {
      float v = speaker_signal_[static_cast<size_t>(i)];
      if (v == 0.0f) continue;
      size_t left_base = static_cast<size_t>(i + speaker.left_delay);
      size_t right_base = static_cast<size_t>(i + speaker.right_delay);
      for (size_t k = 0; k < ir_len; k++) {
	left_acc_[left_base + k] += v * speaker.left_ir[k];
	right_acc_[right_base + k] += v * speaker.right_ir[k];
      }
    }
  }

  SampleData out(getOutChannels(), frames);
  auto out_left = out.getChannelData(0), out_right = out.getChannelData(1);
  for (int i = 0; i < frames; i++) {
    float l = left_acc_[static_cast<size_t>(i)] * kMasterGainTrim;
    float r = right_acc_[static_cast<size_t>(i)] * kMasterGainTrim;
    if (l > 1.0f) l = 1.0f; else if (l < -1.0f) l = -1.0f;
    if (r > 1.0f) r = 1.0f; else if (r < -1.0f) r = -1.0f;
    out_left[i] = l;
    out_right[i] = r;
  }
  out.setNonZero();

  for (size_t i = 0; i < tail_len; i++) {
    left_tail_[i] = left_acc_[static_cast<size_t>(frames) + i];
    right_tail_[i] = right_acc_[static_cast<size_t>(frames) + i];
  }

  return out;
}
