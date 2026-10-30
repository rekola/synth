// Render-only executable (the Windows build today): no terminal UI, audio
// device, MIDI or Launchpad, just `synth --render out.wav song.xml`.
#include "Controller.h"
#include "ambisonic/AmbisonicEncoding.h"
#include "cli/CommonOptions.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <fmt/core.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

using namespace std;

// Command-line arguments as UTF-8 (Windows hands narrow argv over in the ANSI
// code page, which loses non-ASCII file names).
static vector<string>
utf8Arguments(int argc, char * argv[]) {
  vector<string> args;
#ifdef _WIN32
  (void)argc;
  (void)argv;
  int wargc = 0;
  LPWSTR * wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  for (int i = 0; wargv && i < wargc; i++) {
    int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
    string s(static_cast<size_t>(n > 0 ? n : 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), n, nullptr, nullptr);
    s.resize(strlen(s.c_str()));
    args.push_back(s);
  }
  if (wargv) LocalFree(wargv);
#else
  for (int i = 0; i < argc; i++) args.push_back(argv[i]);
#endif
  return args;
}

static void
printUsage() {
  fmt::print(stderr,
             "usage: synth --render out.wav [options] song.xml\n");
  printCommonOptionsUsage(stderr);
  fmt::print(stderr,
             "  --help, -h             this text\n"
             "This build only renders; realtime playback and the UI are not available here.\n");
}

int main(int argc, char * argv[]) {
  auto args = utf8Arguments(argc, argv);
  vector<char *> argv8;
  for (auto & a : args) argv8.push_back(a.data());
  argv8.push_back(nullptr);
  argc = static_cast<int>(args.size());
  argv = argv8.data();

  ChannelConfiguration channel_config(48000, kAmbisonicOrder);
  CommonOptions common;
  vector<string> input;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printUsage();
      return 0;
    } else if (parseCommonOption(argc, argv, i, common, channel_config)) {
    } else if (argv[i][0] == '-') {
      fmt::print(stderr, "invalid parameter {} (this build only renders)\n", argv[i]);
      return 1;
    } else {
      input.push_back(argv[i]);
    }
  }

  if (common.show_licenses) {
    printLicenses();
    return 0;
  }

  if (common.render_path.empty() || input.empty()) {
    printUsage();
    return 1;
  }

  auto controller = make_shared<Controller>(channel_config);
  applyCommonOptions(common, *controller);
  if (!controller->openSong(input.front())) return 1;
  return renderSongToWav(*controller, channel_config, common.render_path) ? 0 : 1;
}
