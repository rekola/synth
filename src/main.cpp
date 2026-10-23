#ifdef _WIN32
#include "ui/Theme.h"
#include "audio/WasapiAudio.h"
#else
#include "audio/AlsaAudio.h"
#endif
#include "audio/NullAudio.h"
#include "audio/AudioDevices.h"
#include "audio/DeviceSettings.h"
#include "launchpad/LaunchpadIO.h"
#include "midi/MidiInput.h"
#include "midi/MidiPorts.h"
#include "launchpad/LaunchpadManager.h"
#ifdef SYNTH_TERMINAL_UI
#include "ui/tui/TerminalUI.h"
#endif
#include "ui/headless/HeadlessUI.h"
#include "util/Daemon.h"
#include "util/ShutdownSignal.h"
#include "Controller.h"
#include "util/StderrLogger.h"
#include "audio/OfflineRenderer.h"
#include "ambisonic/AmbisonicEncoding.h"
#include "cli/CommonOptions.h"
#include "util/Paths.h"

#include <cstring>
#include <filesystem>
#include <signal.h>
#ifdef SYNTH_TERMINAL_UI
#include <termios.h>
#include <unistd.h>
#endif

#ifdef SYNTH_TERMINAL_UI
#include <cellui/term/TerminalSession.h>
#ifdef SYNTH_GTK_UI
#include <cellui/gtk/Window.h>
#endif
#endif
#include <cellui/BackendChoice.h>
#include <cellui/term/ImageEncoder.h>
#include <fmt/core.h>

using namespace std;

int main(int argc, char *argv[]) {
  // 48000, not 44100: the "default" ALSA device is commonly PipeWire's
  // ALSA-compat plugin, whose own graph runs at a fixed native rate
  // (48000 on a stock PipeWire install - see pipewire.conf's
  // default.clock.rate) - requesting 44100 forces an extra resample stage
  // in that plugin, adding latency (and a little quality loss) for no
  // benefit. --samplerate still overrides this.
  ChannelConfiguration channel_config(48000, kAmbisonicOrder); // default to the highest supported order
  CommonOptions common;                                        // --stereo, --legacy-binaural, --licenses, --render
  string theme_name = "dark"; // --theme dark|light|original: the terminal UI's colours
  bool theme_given = false;
  // --reduced-motion (or SYNTH_REDUCED_MOTION=1): no easing, no pulsing.
  bool reduced_motion = getenv("SYNTH_REDUCED_MOTION") != nullptr && strcmp(getenv("SYNTH_REDUCED_MOTION"), "0") != 0;
  cellui::BackendKind ui_kind = cellui::BackendKind::AUTO; // --ui auto|terminal|raster: which backend the UI runs on
  UI::View initial_view = UI::View::LIVE; // --view live|arrangement: which view the UI starts in
  // --dither nearest|ordered|floyd-steinberg: how what has to meet the
  // terminal's palette (Sixel images) is dithered.
  auto dither = cellui::term::Dither::Method::ORDERED;
  // The saved audio/MIDI device choices; --capture-device/--playback-device/
  // --midi-input override them for this run only (`devices`), never the file
  // (`saved_devices`, what the Controller persists from).
  auto devices_path = defaultDeviceSettingsPath();
  auto saved_devices = loadDeviceSettings(devices_path);
  auto devices = saved_devices;
  bool list_devices = false; // --list-devices: print what can be selected and exit
  vector<string> input;
  bool headless = false; // --headless: no terminal UI, status lines on stderr
  bool daemon = false; // --daemon: --headless, detached from the terminal
  bool autoplay = false; // --autoplay: start the transport once running
  bool null_audio = false; // --null-audio: no sound device, blocks paced by the clock
  string log_file; // --log-file: where a daemon's stderr goes
  string pid_file; // --pid-file: where a daemon writes its pid

  for (int i = 1; i < argc; i++) {
    if (parseCommonOption(argc, argv, i, common, channel_config)) {
    } else if (strcmp(argv[i], "--view") == 0) {
      if (i + 1 < argc && strcmp(argv[i + 1], "live") == 0) initial_view = UI::View::LIVE;
      else if (i + 1 < argc && strcmp(argv[i + 1], "arrangement") == 0) initial_view = UI::View::ARRANGEMENT;
      else {
	fmt::print(stderr, "--view requires 'live' or 'arrangement'\n");
	exit(1);
      }
      i++;
    } else if (strcmp(argv[i], "--ui") == 0) {
      if (i + 1 >= argc || !cellui::parseBackendKind(argv[i + 1], ui_kind)) {
        fmt::print(stderr, "--ui requires 'auto', 'terminal' or 'raster'\n");
        exit(1);
      }
      i++;
    } else if (strcmp(argv[i], "--dither") == 0) {
      if (i + 1 >= argc || !cellui::term::Dither::byName(argv[i + 1], dither)) {
        fmt::print(stderr, "--dither requires 'nearest', 'ordered' or 'floyd-steinberg'\n");
        exit(1);
      }
      i++;
    } else if (strcmp(argv[i], "--theme") == 0) {
      Theme probe;
      if (i + 1 >= argc || !Theme::byName(argv[i + 1], probe)) {
	fmt::print(stderr, "--theme requires 'dark', 'light' or 'original'\n");
	exit(1);
      }
      theme_name = argv[i + 1];
      theme_given = true;
      i++;
    } else if (strcmp(argv[i], "--reduced-motion") == 0) {
      reduced_motion = true;
    } else if (strcmp(argv[i], "--headless") == 0) {
      headless = true;
    } else if (strcmp(argv[i], "--daemon") == 0) {
      headless = daemon = true;
    } else if (strcmp(argv[i], "--autoplay") == 0) {
      autoplay = true;
    } else if (strcmp(argv[i], "--null-audio") == 0) {
      null_audio = true;
    } else if (strcmp(argv[i], "--log-file") == 0 || strcmp(argv[i], "--pid-file") == 0) {
      bool is_log = strcmp(argv[i], "--log-file") == 0;
      if (i + 1 >= argc) {
	fmt::print(stderr, "{} requires a file name\n", argv[i]);
	exit(1);
      }
      (is_log ? log_file : pid_file) = argv[++i];
    } else if (strcmp(argv[i], "--capture-device") == 0 || strcmp(argv[i], "--playback-device") == 0 ||
               strcmp(argv[i], "--midi-input") == 0) {
      if (i + 1 >= argc) {
        fmt::print(stderr, "{} requires a name (see --list-devices)\n", argv[i]);
        exit(1);
      }
      string * target = strcmp(argv[i], "--capture-device") == 0 ? &devices.capture : strcmp(argv[i], "--playback-device") == 0 ? &devices.playback
                                                                                                                                : &devices.midi_input;
      *target = argv[++i];
    } else if (strcmp(argv[i], "--list-devices") == 0) {
      list_devices = true;
    } else if (argv[i][0] == '-') {
      fmt::print(stderr, "invalid parameter\n");
      exit(1);
    } else {
      input.push_back(argv[i]);
    }
  }

  if (common.show_licenses) {
    printLicenses();
    return 0;
  }

  if (list_devices) {
    fmt::print("{}\n", pipeWireAvailable() ? "Audio devices (PipeWire):" : "Audio devices (ALSA - PipeWire not available):");
    auto print = [](const char * heading, const char * flag, const auto & entries) {
      fmt::print("\n{}:\n", heading);
      for (auto & entry : entries) {
        fmt::print("  {}\n      {} \"{}\"\n", entry.label, flag, entry.name.empty() ? "default" : entry.name);
      }
    };
    print("Inputs", "--capture-device", listCaptureDevices());
    print("Outputs", "--playback-device", listPlaybackDevices());
    fmt::print("\nMIDI inputs:\n");
    for (auto & source : listMidiSources()) fmt::print("  {}\n      --midi-input \"{}\"\n", source.label, source.spec);
    fmt::print("\nSelections made in the program are saved in {}\n", devices_path.empty() ? "(no config directory)" : devices_path);
    return 0;
  }

  if (!common.render_path.empty()) {
    // Offline render never touches a real audio device (see the --render
    // section of CLAUDE.md), so there's nothing to negotiate - channel_config
    // stays exactly as requested.
    if (input.empty()) {
      fmt::print(stderr, "--render requires a song file\n");
      exit(1);
    }
    auto controller = make_shared<Controller>(channel_config);
    applyCommonOptions(common, *controller);
    // Song::open() already printed the actual reason (missing file,
    // invalid XML, malformed command data, ...) to stderr - no separate
    // message here, which used to unconditionally claim "not found"
    // regardless of which of those it actually was.
    if (!controller->openSong(input.front())) exit(1);
    return renderSongToWav(*controller, channel_config, common.render_path) ? 0 : 1;
  }

  if ((!log_file.empty() || !pid_file.empty()) && !daemon) {
    fmt::print(stderr, "--log-file and --pid-file require --daemon\n");
    exit(1);
  }

  if (autoplay && !headless) {
    fmt::print(stderr, "--autoplay requires --headless\n");
    exit(1);
  }

  // Detach before any thread or audio handle exists - fork() only carries
  // the calling thread over. Returns in the daemon process only; the
  // original exits once startup finishes or fails (see daemonStarted()).
  if (daemon) daemonize(log_file, pid_file);

  StderrLogger logger;

  // Initialize the real audio device *before* constructing Controller/
  // Player: AlsaAudio::initialize() can negotiate a different sample rate
  // than requested (see AlsaAudio.cpp), and every sample-rate-dependent
  // computation downstream (tempo/row duration, oscillator/SF2 pitch, ...)
  // needs to be built against whatever the device actually agreed to -
  // Controller copies channel_config at construction time, so this has to
  // happen first, not be patched up after the fact.
  unique_ptr<AudioAPI> audio_device;
  if (null_audio) {
    audio_device = make_unique<NullAudio>(channel_config.getAudioOutSampleRate(), channel_config.getDeviceChannels());
  } else {
#ifdef _WIN32
    auto device = make_unique<WasapiAudio>(channel_config.getAudioOutSampleRate(), channel_config.getDeviceChannels());
#else
    auto device = make_unique<AlsaAudio>(channel_config.getAudioOutSampleRate(), channel_config.getDeviceChannels());
#endif
    device->initialize(logger, devices);
    audio_device = std::move(device);
  }
  AudioAPI & audio = *audio_device;
  channel_config.setAudioOutSampleRate(audio.getFrequency());

  auto controller = make_shared<Controller>(channel_config);
  controller->setDeviceSettings(saved_devices, devices_path);
  applyCommonOptions(common, *controller);

  if (!input.empty()) {
    // Every positional argument opens its own buffer (openSong() switches
    // the active buffer to whichever it just opened/found, without closing
    // any buffer already open - see Controller::addBuffer()), so multiple
    // files on the command line land as multiple open buffers, the last one
    // ending up active. Mainly useful for testing multi-buffer behavior
    // without going through the UI's own buffer-switching commands.
    for (const auto & path : input) {
      // Song::open() already printed the actual reason - see the --render
      // path's own comment above.
      if (!controller->openSong(path)) exit(1);
    }
  } else {
    // No file given on the command line - open the bundled welcome song by
    // default: the project-local, cwd-relative copy first (running
    // straight out of the source tree, findDefaultSoundFont()'s own
    // data/-override precedent in Controller.cpp), then the `make
    // install`ed copy (paths::installedWelcomeSongPath(), baked in at configure
    // time - see InstallPaths.h.in), falling back to a fresh empty buffer
    // if neither is there. No separate "new song" entry point otherwise
    // (see Controller.h's own switchToBuffer() comment) - freshBufferName()
    // picks a buffer name nothing has open yet (trivially "song.xml" this
    // early), and switchToBuffer() creates it since it doesn't recognize
    // the name.
    error_code ec;
    string welcome_song;
    if (filesystem::is_regular_file("songs/welcome.xml", ec)) welcome_song = "songs/welcome.xml";
    else if (filesystem::is_regular_file(paths::installedWelcomeSongPath(), ec)) welcome_song = paths::installedWelcomeSongPath();
    if (welcome_song.empty() || !controller->openSong(welcome_song)) {
      controller->switchToBuffer(controller->freshBufferName());
    }
  }

  auto midi = createMidiInput(logger);
  midi->setSource(devices.midi_input, logger);

  LaunchpadIO launchpad_io(createMidiPortBackend());
  launchpad_io.initialize(logger);
  LaunchpadManager launchpad_manager;

#if 0
  if (!setlocale(LC_ALL, "")) {
    fmt::print(stderr, "Couldn't set locale\n");
    exit(1);
  }
#endif
   
  if (headless) {
    HeadlessUI ui;
    ui.setAutoplay(autoplay);
    ui.initialize(controller);
    installShutdownSignalHandlers();
    if (daemon) daemonStarted();
    ui.start(audio, *midi, launchpad_io, launchpad_manager);
    return 0;
  }

#ifndef SYNTH_TERMINAL_UI
  (void)ui_kind;
  (void)initial_view;
  (void)theme_name;
  (void)theme_given;
  (void)reduced_motion;
  (void)dither;
  fmt::print(stderr, "This build has no terminal UI; use --headless\n");
  return 1;
#else
  std::unique_ptr<cellui::Backend> backend;
  // Without --theme, the colours follow the backend's own: light on a
  // light background.
  bool light = false;
  cellui::BackendKind chosen = cellui::BackendKind::TERMINAL;
  // The backend is chosen once, here: a named one that cannot start is an
  // error, and only `auto` goes on to the next candidate.
  auto candidates = cellui::backendCandidates(ui_kind, cellui::probeBackendEnvironment());
  for (size_t i = 0; i < candidates.size() && !backend; i++) {
    std::string ui_error;
    chosen = candidates[i];
    bool last = i + 1 == candidates.size();
    if (chosen == cellui::BackendKind::RASTER) {
#ifdef SYNTH_GTK_UI
      backend = cellui::gtk::Window::open({}, &ui_error);
      if (backend) {
        if (auto bg = backend->display().default_bg) light = bg->luminance() > 0.2f;
        break;
      }
#else
      ui_error = "this build has no window UI (it needs GTK 4.10 or later; see SYNTH_ENABLE_GTK)";
#endif
    } else {
      cellui::term::SessionOptions session_options;
      session_options.dither = dither;
      auto session = cellui::term::TerminalSession::open(session_options, &ui_error);
      if (session) {
        light = session->capabilities().appearance == cellui::term::Appearance::LIGHT;
        backend = std::move(session);
        break;
      }
    }
    if (last || ui_kind != cellui::BackendKind::AUTO) {
      fmt::print(stderr, "Cannot start the {} UI: {}\n", cellui::backendName(chosen), ui_error);
      return 1;
    }
  }

  // After the backend is up, so these replace its handlers for the
  // termination signals: a SIGTERM/SIGHUP then ends the main loop and
  // unwinds normally (the terminal gets restored, Launchpad LEDs get
  // cleared) instead of the process being killed.
  installShutdownSignalHandlers();

  if (!theme_given) theme_name = light ? "light" : "dark";
  TerminalUI ui(*backend);
  ui.setInitialView(initial_view);
  ui.setTheme(theme_name);
  ui.setReducedMotion(reduced_motion);
  ui.initialize(controller);
  ui.start(audio, *midi, launchpad_io, launchpad_manager);

#ifdef SYNTH_GTK_UI
  // SYNTH_WINDOW_STATS=<file>: how long the window's frames took to draw.
  if (chosen == cellui::BackendKind::RASTER) {
    if (const char * stats_path = getenv("SYNTH_WINDOW_STATS")) {
      const auto & stats = static_cast<cellui::gtk::Window &>(*backend).stats();
      if (FILE * f = fopen(stats_path, "a")) {
        double n = static_cast<double>(std::max<size_t>(stats.frames, 1));
        fprintf(f, "frames=%zu draw_ms_mean=%.2f draw_ms_max=%.2f raster_ms_mean=%.2f raster_ms_max=%.2f\n", stats.frames, stats.draw_ms_total / n,
                stats.draw_ms_max, stats.raster_ms_total / n, stats.raster_ms_max);
        fclose(f);
      }
    }
  }
#endif

  return 0;
#endif
}
