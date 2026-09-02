#include "SampleFileLoader.h"

#include "AudioBuffer.h"

#include <sndfile.h>
#include <cstring>
#include <vector>

using namespace std;

namespace {
constexpr sf_count_t kBlockFrames = 4096;
}

LoadedSample
loadMonoSample(const string & path) {
  SF_INFO sfinfo;
  memset(&sfinfo, 0, sizeof(sfinfo));

  SNDFILE * infile = sf_open(path.c_str(), SFM_READ, &sfinfo);
  if (!infile) return {};

  int channels = sfinfo.channels;
  if (channels <= 0) {
    sf_close(infile);
    return {};
  }

  vector<float> block(static_cast<size_t>(kBlockFrames * channels));
  vector<float> mono;
  sf_count_t readcount;
  while ((readcount = sf_readf_float(infile, block.data(), kBlockFrames)) > 0) {
    for (sf_count_t i = 0; i < readcount; i++) {
      // Downmix - every channel averaged into one, not just channel 0
      // (see this file's own header comment for why that matters).
      float sum = 0.0f;
      for (int c = 0; c < channels; c++) sum += block[static_cast<size_t>(i * channels + c)];
      mono.push_back(sum / static_cast<float>(channels));
    }
  }
  sf_close(infile);
  if (mono.empty()) return {};

  auto buffer = make_shared<AudioBuffer>(1, static_cast<int>(mono.size()));
  auto dst = buffer->getChannelData(0);
  for (size_t i = 0; i < mono.size(); i++) dst[i] = mono[i];
  return { buffer, sfinfo.samplerate };
}

bool
writeMonoSample(const string & path, const AudioBuffer & buffer, int sample_rate) {
  SF_INFO info{};
  info.samplerate = sample_rate;
  info.channels = 1;
  info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

  auto file = sf_open(path.c_str(), SFM_WRITE, &info);
  if (!file) return false;

  sf_writef_float(file, buffer.getChannelData(0), static_cast<sf_count_t>(buffer.numberOfFrames()));
  sf_close(file);
  return true;
}
