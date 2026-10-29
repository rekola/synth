#include "../util/FileOpen.h"
#include "CommonOptions.h"

#include "../Controller.h"
#include "../ambisonic/AmbisonicEncoding.h"
#include "../audio/OfflineRenderer.h"
#include "generated/ThirdPartyLicenses.h"

#include <cstdlib>
#include <cstring>

#include <fmt/core.h>
#include <sndfile.h>

using namespace std;

bool parseCommonOption(int argc, char * argv[], int & i, CommonOptions & options, ChannelConfiguration & channel_config) {
  if (strcmp(argv[i], "--samplerate") == 0) {
    int samplerate = 0;
    if (i + 1 < argc && argv[i + 1][0] != '-') {
      i++;
      samplerate = atoi(argv[i]);
    }
    if (!samplerate) {
      fmt::print(stderr, "invalid parameters for samplerate\n");
      exit(1);
    }
    channel_config.setAudioOutSampleRate(samplerate);
  } else if (strcmp(argv[i], "--render") == 0) {
    if (i + 1 < argc) {
      i++;
      options.render_path = argv[i];
    }
    if (options.render_path.empty()) {
      fmt::print(stderr, "--render requires an output file\n");
      exit(1);
    }
  } else if (strcmp(argv[i], "--stereo") == 0) {
    options.force_cardioid = true;
  } else if (strcmp(argv[i], "--legacy-binaural") == 0) {
    options.force_legacy_binaural = true;
  } else if (strcmp(argv[i], "--licenses") == 0) {
    options.show_licenses = true;
  } else if (strcmp(argv[i], "--ambisonic") == 0) {
    int order = kAmbisonicOrder; // bare --ambisonic (no explicit number) means the highest supported order
    if (i + 1 < argc && argv[i + 1][0] != '-') {
      i++;
      order = atoi(argv[i]);
    }
    if (order < 1 || order > kAmbisonicOrder) {
      fmt::print(stderr, "invalid ambisonic order (must be 1-{})\n", kAmbisonicOrder);
      exit(1);
    }
    channel_config.setAmbisonicOrder(order);
  } else {
    return false;
  }
  return true;
}

void printCommonOptionsUsage(FILE * out) {
  fmt::print(out,
             "  --samplerate N         output sample rate (default 48000)\n"
             "  --ambisonic [order]    ambisonic order, 1-{} (default {})\n"
             "  --stereo               cardioid stereo decode instead of binaural\n"
             "  --legacy-binaural      virtual-speaker binaural decoder\n"
             "  --licenses             print third-party licences\n",
             kAmbisonicOrder, kAmbisonicOrder);
}

void printLicenses() {
  fmt::print("{}\n", kThirdPartyLicensesText);
}

void applyCommonOptions(const CommonOptions & options, Controller & controller) {
  if (options.force_cardioid) controller.setMixerType(MixerType::AMBISONIC_STEREO);
  if (options.force_legacy_binaural) controller.setUseLegacyBinaural(true);
}

bool renderSongToWav(Controller & controller, const ChannelConfiguration & channel_config, const string & path) {
  auto & song = controller.getSong();
  auto result = renderSongOffline(song, channel_config, controller.getMixerType(), 1024, 10, 1e-5f, controller.getUseLegacyBinaural());

  if (result.interleaved.empty()) {
    fmt::print(stderr, "Song has nothing to render\n");
    return false;
  }

  SF_INFO info{};
  info.samplerate = result.sampleRate;
  info.channels = result.channels;
  info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
  auto file = openSoundFile(path, SFM_WRITE, &info);
  if (!file) {
    fmt::print(stderr, "Could not open {} for writing: {}\n", path, sf_strerror(nullptr));
    return false;
  }

  sf_writef_float(file, result.interleaved.data(), static_cast<sf_count_t>(result.numberOfFrames()));
  sf_close(file);

  fmt::print(stderr, "Rendered {:.1f} seconds to {}\n",
             static_cast<double>(result.numberOfFrames()) / result.sampleRate, path);
  return true;
}
