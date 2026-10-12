#include "TestFramework.h"

#include "../src/audio/DeviceSettings.h"

#include <string>

using namespace std;

TEST(wasapi_device_names_are_recognised) {
  CHECK(isWasapiDevice("wasapi:{0.0.0.00000000}.{abc}"));
  CHECK(wasapiEndpointId("wasapi:{0.0.0.00000000}.{abc}") == "{0.0.0.00000000}.{abc}");
  CHECK(!isWasapiDevice("wasapi:"));
  CHECK(!isWasapiDevice("pw:node"));
  CHECK(!isWasapiDevice("hw:1,0"));
  CHECK(wasapiEndpointId("hw:1,0").empty());
}

#ifdef _WIN32

#include "../src/audio/AudioBuffer.h"
#include "../src/audio/AudioDevices.h"
#include "../src/audio/WasapiAudio.h"
#include "../src/midi/MidiInput.h"
#include "../src/midi/MidiPorts.h"
#include "../src/util/StderrLogger.h"

#include <chrono>

// A CI machine has no sound card or MIDI device, so these check that the
// backends cope with that; what they do on hardware is not covered.

TEST(wasapi_device_lists_start_with_the_default) {
  for (auto & devices : { listPlaybackDevices(), listCaptureDevices() }) {
    CHECK(!devices.empty());
    CHECK(devices[0].name.empty());
    for (size_t i = 1; i < devices.size(); i++) CHECK(isWasapiDevice(devices[i].name));
  }
  CHECK(playbackDeviceExists(""));
  CHECK(!playbackDeviceExists("wasapi:{no-such-endpoint}"));
  CHECK(captureDeviceExists("pw:anything"));
}

TEST(wasapi_without_a_device_waits_and_times_out) {
  StderrLogger logger;
  WasapiAudio audio(48000, 2);
  DeviceSettings devices;
  devices.playback = "wasapi:{no-such-endpoint}";
  audio.initialize(logger, devices);

  Notifier wake;
  auto before = std::chrono::steady_clock::now();
  auto ready = audio.wait(wake, true, 50);
  auto waited = std::chrono::steady_clock::now() - before;
  if (!audio.hasCaptureDevice() && audio.getPlaybackDeviceName().empty()) {
    CHECK(!ready.woken && !ready.playback && !ready.capture);
    CHECK(waited >= std::chrono::milliseconds(40));
  }
  wake.signal();
  CHECK(audio.wait(wake, false, 1000).woken);
  CHECK(audio.record(logger).size() == 0 || audio.hasCaptureDevice());
}

TEST(winmm_midi_input_without_devices_has_no_events) {
  StderrLogger logger;
  auto input = createMidiInput(logger);
  input->setSource("", logger);
  CHECK(input->readEvents().empty());
  input->setSource("No such keyboard", logger);
  CHECK(input->readEvents().empty());
  for (auto & source : listMidiSources()) CHECK(!source.spec.empty());
}

TEST(winmm_port_backend_opens_and_lists) {
  StderrLogger logger;
  auto backend = createMidiPortBackend();
  CHECK(backend->open(logger));
  for (auto & port : backend->ports()) {
    auto again = backend->port(port.address);
    CHECK(again.has_value());
    CHECK(again->port_name == port.port_name);
  }
  CHECK(!backend->port({999, 0}).has_value());
  CHECK(!backend->connect({999, 0}));
  backend->sendSysEx({999, 0}, {0xF0, 0xF7});
  backend->receive();
}

#endif
