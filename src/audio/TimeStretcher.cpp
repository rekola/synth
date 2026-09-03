#include "TimeStretcher.h"

#include <soundtouch/SoundTouch.h>

using namespace std;

vector<float>
stretchMono(const vector<float> & input, int sample_rate, double tempo_ratio) {
  if (sample_rate <= 0 || tempo_ratio <= 0.0 || tempo_ratio == 1.0 || input.empty()) return input;

  soundtouch::SoundTouch st;
  st.setSampleRate(static_cast<uint>(sample_rate));
  st.setChannels(1);
  st.setTempo(tempo_ratio);

  st.putSamples(input.data(), static_cast<uint>(input.size()));
  st.flush(); // makes every remaining internally-buffered sample available to receiveSamples() below

  vector<float> output;
  // A rough up-front reserve (the algorithm itself decides the real
  // count) - avoids repeated reallocation for the common case without
  // needing to guess exactly.
  output.reserve(static_cast<size_t>(static_cast<double>(input.size()) * tempo_ratio) + 1);

  constexpr uint kChunkFrames = 4096;
  float chunk[kChunkFrames];
  uint n;
  while ((n = st.receiveSamples(chunk, kChunkFrames)) != 0) {
    output.insert(output.end(), chunk, chunk + n);
  }
  return output;
}
