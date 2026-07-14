#include "AlsaAudio.h"
#include "LaunchpadIO.h"
#include "LaunchpadManager.h"
#include "TerminalUI.h"
#include "Controller.h"
#include "StderrLogger.h"
#include "OfflineRenderer.h"

#include <cstring>
#include <signal.h>

#include <sndfile.h>
#include <ncpp/NotCurses.hh>
#include <fmt/core.h>

using namespace std;

// Render the loaded song offline and write it as a WAV file.
static bool renderSongToWav(Controller & controller, const ChannelConfiguration & channel_config, const string & path) {
  auto & song = controller.getSong();
  auto result = renderSongOffline(song, channel_config);

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
  int load_demo = 0;
  bool relative = false;
  ChannelConfiguration channel_config(ChannelConfiguration::STEREO, 44100);
  vector<string> input;
  string render_path;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--demo") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
	i++;
	load_demo = atoi(argv[i]);
      } else {
	load_demo = 1;
      }
    } else if (strcmp(argv[i], "--samplerate") == 0) {
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
    } else if (strcmp(argv[i], "--relative") == 0) {
      relative = true;
    } else if (strcmp(argv[i], "--mono") == 0) {
      channel_config.setType(ChannelConfiguration::MONO);
    } else if (strcmp(argv[i], "--stereo") == 0) {
      channel_config.setType(ChannelConfiguration::STEREO);
    } else if (strcmp(argv[i], "--surround") == 0) {
      channel_config.setType(ChannelConfiguration::SURROUND_5_1);
    } else if (argv[i][0] == '-') {
      fmt::print(stderr, "invalid parameter\n");
      exit(1);
    } else {
      input.push_back(argv[i]);
    }
  }
      
  auto controller = make_shared<Controller>(channel_config);

  if (!input.empty()) {
    if (!controller->openSong(input.front())) {
      fmt::print(stderr, "Could not find file {}\n", input.front());
      exit(1);
    }
  } else if (load_demo == 2) {
    controller->loadDemo2();
  } else {
    controller->createNewSong();
  }

  if (!render_path.empty()) {
    if (input.empty()) {
      fmt::print(stderr, "--render requires a song file\n");
      exit(1);
    }
    return renderSongToWav(*controller, channel_config, render_path) ? 0 : 1;
  }

  StderrLogger logger;
  
  AlsaAudio audio(channel_config.getAudioOutSampleRate(), channel_config.numberOfChannels());
  audio.initialize(logger);

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
  
  TerminalUI ui(nc);
  ui.initialize(controller);
  ui.start(audio, launchpad_io, launchpad_manager);

  return 0;
}
