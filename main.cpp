#include "AlsaAudio.h"
#include "LaunchpadIO.h"
#include "LaunchpadManager.h"
#include "TerminalUI.h"
#include "Controller.h"
#include "StderrLogger.h"
#include "OfflineRenderer.h"
#include "AmbisonicEncoding.h"
#include "generated/ThirdPartyLicenses.h"

#include <cstring>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include <sndfile.h>
#include <ncpp/NotCurses.hh>
#include <fmt/core.h>

using namespace std;

// Render the loaded song offline and write it as a WAV file.
static bool renderSongToWav(Controller & controller, const ChannelConfiguration & channel_config, const string & path) {
  auto & song = controller.getSong();
  auto result = renderSongOffline(song, channel_config, controller.getMixerType(), 1024, 10, 1e-5f, controller.getUseLegacyBinaural());

  if (result.interleaved.empty()) {
    fmt::print(stderr, "Song has no patterns to render\n");
    return false;
  }

  SF_INFO info{};
  info.samplerate = result.sampleRate;
  info.channels = result.channels;
  info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
  auto file = sf_open(path.c_str(), SFM_WRITE, &info);
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

int main(int argc, char *argv[]) {
  // 48000, not 44100: the "default" ALSA device is commonly PipeWire's
  // ALSA-compat plugin, whose own graph runs at a fixed native rate
  // (48000 on a stock PipeWire install - see pipewire.conf's
  // default.clock.rate) - requesting 44100 forces an extra resample stage
  // in that plugin, adding latency (and a little quality loss) for no
  // benefit. --samplerate still overrides this.
  ChannelConfiguration channel_config(48000, kAmbisonicOrder); // default to the highest supported order
  bool force_cardioid = false; // --stereo: skip binaural HRTF decode even if available
  bool force_legacy_binaural = false; // --legacy-binaural: use the old virtual-speaker-rig decoder instead of MagLS
  bool show_licenses = false; // --licenses: print third-party license text and exit
  vector<string> input;
  string render_path;

  for (int i = 1; i < argc; i++) {
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
	render_path = argv[i];
      }
      if (render_path.empty()) {
	fmt::print(stderr, "--render requires an output file\n");
	exit(1);
      }
    } else if (strcmp(argv[i], "--stereo") == 0) {
      force_cardioid = true;
    } else if (strcmp(argv[i], "--legacy-binaural") == 0) {
      force_legacy_binaural = true;
    } else if (strcmp(argv[i], "--licenses") == 0) {
      show_licenses = true;
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
    } else if (argv[i][0] == '-') {
      fmt::print(stderr, "invalid parameter\n");
      exit(1);
    } else {
      input.push_back(argv[i]);
    }
  }

  if (show_licenses) {
    fmt::print("{}\n", kThirdPartyLicensesText);
    return 0;
  }

  if (!render_path.empty()) {
    // Offline render never touches a real audio device (see the --render
    // section of CLAUDE.md), so there's nothing to negotiate - channel_config
    // stays exactly as requested.
    if (input.empty()) {
      fmt::print(stderr, "--render requires a song file\n");
      exit(1);
    }
    auto controller = make_shared<Controller>(channel_config);
    if (force_cardioid) controller->setMixerType(MixerType::AMBISONIC_STEREO);
    if (force_legacy_binaural) controller->setUseLegacyBinaural(true);
    if (!controller->openSong(input.front())) {
      fmt::print(stderr, "Could not find file {}\n", input.front());
      exit(1);
    }
    return renderSongToWav(*controller, channel_config, render_path) ? 0 : 1;
  }

  StderrLogger logger;

  // Initialize the real audio device *before* constructing Controller/
  // Player: AlsaAudio::initialize() can negotiate a different sample rate
  // than requested (see AlsaAudio.cpp), and every sample-rate-dependent
  // computation downstream (tempo/row duration, oscillator/SF2 pitch, ...)
  // needs to be built against whatever the device actually agreed to -
  // Controller copies channel_config at construction time, so this has to
  // happen first, not be patched up after the fact.
  AlsaAudio audio(channel_config.getAudioOutSampleRate(), channel_config.getDeviceChannels());
  audio.initialize(logger);
  channel_config.setAudioOutSampleRate(audio.getFrequency());

  auto controller = make_shared<Controller>(channel_config);
  if (force_cardioid) controller->setMixerType(MixerType::AMBISONIC_STEREO);
  if (force_legacy_binaural) controller->setUseLegacyBinaural(true);

  if (!input.empty()) {
    if (!controller->openSong(input.front())) {
      fmt::print(stderr, "Could not find file {}\n", input.front());
      exit(1);
    }
  } else {
    controller->createNewSong();
  }

  LaunchpadIO launchpad_io;
  launchpad_io.initialize(logger);
  LaunchpadManager launchpad_manager;

#if 0
  if (!setlocale(LC_ALL, "")) {
    fmt::print(stderr, "Couldn't set locale\n");
    exit(1);
  }
#endif
   
  // notcurses_options nopts{};
  // nopts.flags = NCOPTION_INHIBIT_SETLOCALE;
  auto nc = make_shared<ncpp::NotCurses>();
  nc->mouse_enable(NCMICE_ALL_EVENTS);
  nc->linesigs_disable();

  // notcurses's own raw-mode setup leaves IXON (software flow control) on -
  // confirmed via direct termios inspection, not just inferred - so Ctrl-S/
  // Ctrl-Q (needed for the C-x C-s save binding, UI.cpp) get intercepted by
  // the kernel tty driver as XOFF/XON (pausing/resuming terminal output)
  // instead of ever reaching notcurses's input decoder as keystrokes, the
  // same class of "a control character doesn't reach the app" problem
  // linesigs_disable() above already solves for Ctrl-C/Ctrl-Z. Must run
  // after NotCurses's own constructor, which does its own termios setup -
  // doing this first would just get overwritten.
  if (isatty(STDIN_FILENO)) {
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) == 0) {
      t.c_iflag &= ~(tcflag_t)(IXON | IXOFF);
      tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
  }
  
  TerminalUI ui(nc);
  ui.initialize(controller);
  ui.start(audio, launchpad_io, launchpad_manager);

  return 0;
}
