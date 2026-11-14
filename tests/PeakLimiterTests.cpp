#include "TestFramework.h"

#include "../src/dsp/PeakLimiter.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/Note.h"
#include "../src/state/SongState.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"

#ifdef SYNTH_HAVE_LIBMYSOFA
#include "../src/ambisonic/AmbisonicBinauralMixer.h"
#include "../src/ambisonic/AmbisonicMagLSDecoder.h"
#endif

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace std;

// Below the ceiling it is exact unity: a song that never reaches it renders
// as it did before the limiter.
TEST(peak_limiter_passes_what_stays_under_the_ceiling_unchanged) {
  dsp::PeakLimiter limiter(48000);
  for (int i = 0; i < 48000; i++) {
    float l = 0.85f * sinf(static_cast<float>(i) * 0.01f), r = -l;
    const float in_l = l, in_r = r;
    limiter.process(l, r);
    CHECK(l == in_l && r == in_r);
  }
  CHECK(limiter.atRest());
}

// Over it, nothing passes the ceiling, both channels get one gain, and the
// gain holds, then recovers to unity.
TEST(peak_limiter_keeps_loud_output_at_its_ceiling_and_recovers) {
  const int rate = 48000;
  dsp::PeakLimiter limiter(rate);
  const float ceiling = dsp::PeakLimiter::ceiling();
  CHECK(fabsf(ceiling - powf(10.0f, -1.0f / 20.0f)) < 1e-6f);
  float loudest = 0.0f;
  for (int i = 0; i < rate / 2; i++) {
    float l = 3.0f * sinf(static_cast<float>(i) * 2.0f * static_cast<float>(M_PI) * 110.0f / rate), r = 0.5f * l;
    const float ratio = r / l;
    limiter.process(l, r);
    loudest = max(loudest, max(fabsf(l), fabsf(r)));
    if (fabsf(l) > 1e-3f) CHECK(fabsf(r / l - ratio) < 1e-4f); // the image stays
  }
  CHECK(loudest <= ceiling + 1e-6f);
  CHECK(loudest > ceiling - 0.01f); // it limits, it does not squash
  // A last peak as loud as the ones before starts the hold afresh.
  float l = 3.0f, r = 1.5f;
  limiter.process(l, r);
  const float held = limiter.gain();
  CHECK(held < 0.4f);
  limiter.skip(static_cast<int>(dsp::PeakLimiter::kHoldSeconds * rate) - 2);
  CHECK(limiter.gain() == held);
  limiter.skip(static_cast<int>(20 * dsp::PeakLimiter::kReleaseSeconds * rate));
  CHECK(limiter.atRest());
}

// One cycle of a low note does not duck and recover on each of its peaks.
TEST(peak_limiter_holds_through_a_low_notes_cycle) {
  const int rate = 48000;
  dsp::PeakLimiter limiter(rate);
  float lowest = 1.0f, highest = 0.0f;
  for (int i = 0; i < rate; i++) {
    float l = 2.0f * sinf(static_cast<float>(i) * 2.0f * static_cast<float>(M_PI) * 41.0f / rate), r = l;
    limiter.process(l, r);
    if (i > rate / 2) {
      lowest = min(lowest, limiter.gain());
      highest = max(highest, limiter.gain());
    }
  }
  CHECK(highest - lowest < 0.02f);
}

// A song loud enough to pass full scale comes out at the ceiling, rather
// than clipped, through every decoder: the cardioid one, and with libmysofa
// and a SOFA file the binaural ones (MagLS and the legacy speaker rig), built
// directly so a missing file cannot fall back to cardioid unnoticed.
TEST(a_loud_song_is_limited_not_clipped) {
  Song song(Tuning::EDO12);
  song.addInstrument(make_unique<Oscillator>(WaveformType::SQUARE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto arrangement = song.getArrangement();
  for (int column = 0; column < 6; column++) arrangement.setNote(0, track.getInternalId(), column, Note(48 + 7 * column, 255));

  ChannelConfiguration config(48000, 3);
  vector<pair<const char *, unique_ptr<Mixer>>> mixers;
  mixers.emplace_back("cardioid", createMixer(config, MixerType::AMBISONIC_STEREO));
#ifdef SYNTH_HAVE_LIBMYSOFA
  auto magls = make_unique<AmbisonicMagLSDecoder>(config.numberOfChannels(), config.getAudioOutSampleRate());
  auto legacy = make_unique<AmbisonicBinauralMixer>(config.numberOfChannels(), config.getAudioOutSampleRate());
  if (magls->isReady() && legacy->isReady()) {
    mixers.emplace_back("MagLS", std::move(magls));
    mixers.emplace_back("legacy binaural", std::move(legacy));
  } else {
    fprintf(stderr, "  no SOFA file: the binaural decoders are not checked\n");
  }
#endif

  for (auto & [name, mixer] : mixers) {
    SongState state(config);
    state.initialize(song);
    state.setIsPlaying(true);
    float loudest = 0.0f;
    int at_full_scale = 0;
    for (int block = 0; block < 40; block++) {
      state.renderBlock(512, song, *mixer);
      auto out = mixer->encode();
      for (int c = 0; c < out.numberOfChannels(); c++)
        for (int i = 0; i < out.numberOfFrames(); i++) {
          const float v = fabsf(out.getChannelData(c)[i]);
          loudest = max(loudest, v);
          if (v >= 1.0f) at_full_scale++;
        }
    }
    fprintf(stderr, "  %s: loudest %.4f\n", name, loudest);
    CHECK(loudest > 0.5f);
    CHECK(loudest <= dsp::PeakLimiter::ceiling() + 1e-6f);
    CHECK(at_full_scale == 0);
  }
}
