#ifndef _AMBISONICDECODERS_H_
#define _AMBISONICDECODERS_H_

#include "Mixer.h"
#include "AmbisonicEncoding.h"

#include <memory>

// Always available (no libmysofa dependency): accumulates the ambisonic
// bus, then cheaply decodes it to stereo via decodeToStereo()'s cardioid
// L/R matrix (AmbisonicEncoding.h). Used directly when the binaural
// decoder isn't compiled in or no SOFA file resolves, and selectable at
// runtime as an explicit "bypass HRTF" choice either way.
class AmbisonicStereoMixer : public Mixer {
 public:
  AmbisonicStereoMixer(int ambisonic_channels, int outSampleRate)
    : Mixer(2, outSampleRate), ambisonic_channels_(ambisonic_channels), buffer_(static_cast<short>(ambisonic_channels), 0) { }

  void reset() override {
    buffer_.zero();
  }

  // mixNamed() rather than mix() - `input` (a track's rendered output) may
  // carry AuxA/AuxB trailing its regular ambisonic channels (see
  // Mixer.h); buffer_ never marks them present (constructed via the plain
  // raw-count constructor) so they're silently ignored here.
  void accumulate(const SampleData & input) override {
    if (buffer_.numberOfFrames() != input.numberOfFrames()) {
      buffer_ = SampleData(static_cast<short>(ambisonic_channels_), input.numberOfFrames());
      buffer_.zero();
    }
    buffer_.mixNamed(input);
  }

  const SampleData & getRawBus() const override { return buffer_; }

  SampleData encode() override {
    SampleData out(static_cast<short>(getOutChannels()), buffer_.numberOfFrames());
    decodeToStereo(buffer_, out);

    for (int c = 0; c < out.numberOfChannels(); c++) {
      auto data = out.getChannelData(c);
      for (int i = 0; i < out.numberOfFrames(); i++) {
	if (data[i] > 1.0f) data[i] = 1.0f;
	else if (data[i] < -1.0f) data[i] = -1.0f;
      }
    }
    return out;
  }

 private:
  int ambisonic_channels_;
  SampleData buffer_;
};

#endif
