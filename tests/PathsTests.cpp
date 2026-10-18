#include "TestFramework.h"

#include "../src/audio/DeviceSettings.h"
#include "../src/util/Paths.h"
#include "../src/util/ThreadSupport.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <thread>

#ifndef _WIN32
#include <pthread.h>
#endif

using namespace std;
namespace fs = std::filesystem;

namespace {

// Sets or, with nullptr, removes an environment variable for a test.
void setVariable(const char * name, const char * value) {
#ifdef _WIN32
  _putenv_s(name, value ? value : "");
#else
  if (value) setenv(name, value, 1);
  else unsetenv(name);
#endif
}

// Restores the variables a test touched when it ends.
struct SavedVariable {
  string name;
  optional<string> value;
  explicit SavedVariable(const char * n) : name(n) {
    if (auto v = getenv(n)) value = v;
  }
  ~SavedVariable() { setVariable(name.c_str(), value ? value->c_str() : nullptr); }
};

} // namespace

#ifdef _WIN32

TEST(config_and_data_dirs_follow_the_windows_environment) {
  SavedVariable appdata("APPDATA"), local("LOCALAPPDATA");
  setVariable("APPDATA", "C:\\Users\\me\\AppData\\Roaming");
  setVariable("LOCALAPPDATA", "C:\\Users\\me\\AppData\\Local");
  CHECK(paths::configDir() == fs::path("C:\\Users\\me\\AppData\\Roaming") / "synth");
  CHECK(paths::userDataDir() == fs::path("C:\\Users\\me\\AppData\\Local") / "synth");
  CHECK(fs::path(defaultDeviceSettingsPath()) == paths::configDir() / "devices.conf");

  setVariable("APPDATA", nullptr);
  setVariable("LOCALAPPDATA", nullptr);
  CHECK(paths::configDir().empty());
  CHECK(paths::userDataDir().empty());
  CHECK(defaultDeviceSettingsPath().empty());
}

TEST(installed_data_on_windows_is_next_to_the_executable) {
  CHECK(!paths::executableDir().empty());
  CHECK(paths::installDataDir() == paths::executableDir() / "share");
  CHECK(fs::path(paths::installedWelcomeSongPath()) == paths::installDataDir() / "synth" / "songs" / "welcome.xml");
}

#else

TEST(config_dir_prefers_xdg_then_home) {
  SavedVariable xdg("XDG_CONFIG_HOME"), home("HOME");
  setVariable("XDG_CONFIG_HOME", "/tmp/xdg");
  setVariable("HOME", "/home/me");
  CHECK(paths::configDir() == fs::path("/tmp/xdg/synth"));
  CHECK(defaultDeviceSettingsPath() == "/tmp/xdg/synth/devices.conf");

  setVariable("XDG_CONFIG_HOME", "");
  CHECK(paths::configDir() == fs::path("/home/me/.config/synth"));
  CHECK(defaultDeviceSettingsPath() == "/home/me/.config/synth/devices.conf");

  setVariable("HOME", nullptr);
  CHECK(paths::configDir().empty());
  CHECK(defaultDeviceSettingsPath().empty());
}

TEST(data_dirs_on_linux_are_the_home_share_and_usr_share) {
  SavedVariable home("HOME");
  setVariable("HOME", "/home/me");
  CHECK(paths::userDataDir() == fs::path("/home/me/.local/share"));
  CHECK(paths::installDataDir() == fs::path("/usr/share"));
  setVariable("HOME", nullptr);
  CHECK(paths::userDataDir().empty());
}

TEST(executable_dir_names_the_directory_of_the_running_binary) {
  auto dir = paths::executableDir();
  CHECK(!dir.empty());
  CHECK(fs::exists(dir / "synth_tests"));
}

#endif

TEST(thread_names_can_be_set_and_audio_priority_is_harmless) {
  setCurrentThreadName("synth-test");
#ifndef _WIN32
  char name[32] = {};
  pthread_getname_np(pthread_self(), name, sizeof(name));
  CHECK(string(name) == "synth-test");
#endif
  // Whether it is granted depends on the system; asking must always be safe,
  // on a thread of its own as the audio thread does.
  thread audio([] {
    setCurrentThreadName("synth-audio");
    AudioThreadPriority priority;
    (void)priority.granted();
  });
  audio.join();
}
