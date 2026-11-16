#include "TestFramework.h"

#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/SphericalPosition.h"
#include "../src/instruments/Additive.h"
#include "../src/instruments/FM.h"
#include "../src/instruments/InstrumentVoice.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/PadSynth.h"
#include "../src/instruments/WaveformType.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/Song.h"
#include "../src/state/SongState.h"

#include <cmath>
#include <string>
#include <vector>

using namespace std;

namespace {

// Mean frequency of the left channel over [from, to), from rising zero crossings.
double measuredHz(const AudioBuffer & out, int from, int to) {
  const float * left = out.getChannelData(0);
  vector<double> times;
  for (int i = max(from, 1); i < to; i++) {
    if (left[i - 1] < 0 && left[i] >= 0) times.push_back(i - 1 + static_cast<double>(-left[i - 1]) / static_cast<double>(left[i] - left[i - 1]));
  }
  if (times.size() < 3) return 0.0;
  return 44100.0 * static_cast<double>(times.size() - 1) / (times.back() - times.front());
}

// A held sine note (31-EDO A6, 1760 Hz) with `fx`, rendered through the whole
// song state; its frequency in Hz. A slow tempo gives the measurement a long window.
double hzWithFx(const string & fx) {
  Song song(Tuning::EDO31);
  song.setTempo(30);
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto arrangement = song.getArrangement();
  Note note(240, 100);
  if (!fx.empty()) CHECK(note.setFx(fx));
  arrangement.setNote(0, track.getInternalId(), 0, note);

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);
  int row_samples = config.getSampleInterval(song.getTempo());
  state.renderBlock(3 * row_samples, song, *mixer);
  auto out = mixer->encode();
  return measuredHz(out, 2000, 3 * row_samples - 100);
}

double centsBetween(double hz, double reference) { return 1200.0 * log2(hz / reference); }

} // namespace

// The correction reaches the oscillator as the exact amount: fine in sixteenths
// of a cent, coarse in whole cents, each within a thousandth of a cent.
TEST(a_tuning_correction_sounds_at_its_exact_amount_at_the_oscillator) {
  double plain = hzWithFx("");
  CHECK(plain > 0.0);
  struct Case {
    const char * fx;
    double cents;
  };
  for (auto c : {Case{"+10", 1.0}, Case{"+01", 1.0 / 16}, Case{"-FF", -15.9375}, Case{"+A8", 10.5}, Case{"+00", 0.0},
                 Case{">10", 16.0}, Case{"<FF", -255.0}, Case{">01", 1.0}}) {
    double measured = centsBetween(hzWithFx(c.fx), plain);
    CHECK_NEAR(measured, c.cents, 0.001);
  }
}

namespace {

// getFrequency() is protected; naming it through a derived class reaches it.
struct VoicePeek : InstrumentVoice {
  static float frequency(const InstrumentVoice & voice) { return (voice.*(&VoicePeek::getFrequency))(); }
};

// The frequency a voice sounds at, relative to the note's own, in cents, when
// the track hands it `detune_cents` as InstrumentTrackState does.
double voiceOffsetCents(const Track & instrument, double detune_cents) {
  ChannelConfiguration config(44100, 1);
  SphericalPosition position;
  position.distance = 1.0f;
  float detune = exp2f(static_cast<float>(detune_cents) / 1200.0f);
  auto voice = instrument.playNote(config, position, SpatialMode::AUTO, Tuning::EDO31, detune, 1.0f, 160, SendLevels{}, NoteCoordinate(0, 0, 0));
  auto * pitched = dynamic_cast<InstrumentVoice *>(voice.get());
  CHECK(pitched != nullptr);
  if (!pitched) return 1e9;
  return centsBetween(static_cast<double>(VoicePeek::frequency(*pitched)), static_cast<double>(getFrequencyFor(Tuning::EDO31, 160)));
}

} // namespace

TEST(every_synthesis_voice_keeps_a_correction_to_a_thousandth_of_a_cent) {
  // The oscillator applies it per array member, so it is measured on the rendered
  // sine above; these three carry it in the voice's own frequency.
  FM fm;
  Additive additive;
  PadSynth pad;
  for (const Track * instrument : {static_cast<const Track *>(&fm), static_cast<const Track *>(&additive), static_cast<const Track *>(&pad)}) {
    for (double cents : {0.0625, -0.0625, 1.4375, -4.75, 15.9375, -15.9375, 31.0, -255.0}) {
      CHECK_NEAR(voiceOffsetCents(*instrument, cents), cents, 0.001);
    }
  }
}
