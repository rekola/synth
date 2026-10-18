#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/audio/DeviceSettings.h"
#include "../src/playback/PlaybackControlEvent.h"

#include <filesystem>
#include <fstream>

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

namespace {

std::string scratchPath(const std::string & name) {
  auto dir = std::filesystem::path(TESTS_SCRATCH_DIR) / "device_settings";
  std::filesystem::create_directories(dir);
  auto path = dir / name;
  std::filesystem::remove(path);
  return path.string();
}

PlaybackControlEvent popControl(EventQueue & queue) {
  auto ptr = queue.pop();
  auto * ev = dynamic_cast<PlaybackControlEvent *>(ptr.get());
  CHECK(ev != nullptr);
  return ev ? *ev : PlaybackControlEvent(PlaybackControlEvent::TERMINATE);
}

} // namespace

TEST(device_names_are_classified) {
  CHECK(isDefaultDevice(""));
  CHECK(isDefaultDevice("default"));
  CHECK(!isDefaultDevice("hw:1,0"));
  CHECK(isPipeWireDevice("pw:alsa_input.usb-Mic"));
  CHECK(!isPipeWireDevice("pw:")); // a prefix with no node is nothing
  CHECK(!isPipeWireDevice("hw:1,0"));
  CHECK(pipeWireNodeName("pw:alsa_input.usb-Mic") == "alsa_input.usb-Mic");
  CHECK(pipeWireNodeName("hw:1,0").empty());
}

TEST(device_settings_round_trip_through_text) {
  DeviceSettings in{"pw:alsa_input.usb-Mic", "pw:alsa_output.pci-0000", "USB MIDI Keyboard:USB MIDI Keyboard MIDI 1"};
  CHECK(parseDeviceSettings(serializeDeviceSettings(in)) == in);
  CHECK(parseDeviceSettings(serializeDeviceSettings(DeviceSettings())) == DeviceSettings());
}

TEST(device_settings_parse_tolerates_comments_blanks_and_unknown_keys) {
  auto parsed = parseDeviceSettings(
      "# a comment\n\n  capture =  pw:mic  \nfuture_key = whatever\nno equals sign here\nplayback=hw:1,0\r\n");
  CHECK(parsed.capture == "pw:mic");
  CHECK(parsed.playback == "hw:1,0");
  CHECK(parsed.midi_input.empty());
}

TEST(device_settings_value_may_contain_spaces_and_equals) {
  auto parsed = parseDeviceSettings("midi_input = Some Synth:Port = 1\n");
  CHECK(parsed.midi_input == "Some Synth:Port = 1");
}

TEST(device_settings_newline_in_a_value_cannot_inject_a_key) {
  DeviceSettings in;
  in.capture = "pw:mic\nplayback = pw:evil";
  auto out = parseDeviceSettings(serializeDeviceSettings(in));
  CHECK(out.playback.empty());
}

TEST(device_settings_save_and_load_a_file) {
  auto path = scratchPath("nested/devices.conf"); // the directory doesn't exist yet
  std::filesystem::remove_all(std::filesystem::path(path).parent_path());
  DeviceSettings in{"pw:a", "pw:b", "c:d"};
  CHECK(saveDeviceSettings(path, in));
  CHECK(loadDeviceSettings(path) == in);
  CHECK(!std::filesystem::exists(path + ".tmp")); // renamed over, not left behind
}

TEST(device_settings_missing_file_is_empty_settings) {
  CHECK(loadDeviceSettings(scratchPath("does_not_exist.conf")) == DeviceSettings());
  CHECK(loadDeviceSettings("") == DeviceSettings());
}

TEST(controller_persists_and_announces_a_capture_choice) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  auto path = scratchPath("controller_capture.conf");
  controller.setDeviceSettings(DeviceSettings{"", "pw:out", ""}, path);

  auto change = controller.setCaptureDevice("pw:in");
  CHECK(change.applied);
  CHECK(change.message.empty());
  CHECK(controller.getDeviceSettings().capture == "pw:in");
  CHECK(loadDeviceSettings(path).capture == "pw:in");
  CHECK(loadDeviceSettings(path).playback == "pw:out"); // the rest is kept

  auto ev = popControl(controller.getPlaybackEventQueue());
  CHECK(ev.getType() == PlaybackControlEvent::SET_CAPTURE_DEVICE);
  CHECK(ev.getBufferName() == "pw:in");
}

TEST(controller_stores_the_default_device_as_empty) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.setDeviceSettings(DeviceSettings{"pw:in", "pw:out", ""}, "");

  CHECK(controller.setPlaybackDevice("default").applied);
  CHECK(controller.getDeviceSettings().playback.empty());
  auto ev = popControl(controller.getPlaybackEventQueue());
  CHECK(ev.getType() == PlaybackControlEvent::SET_PLAYBACK_DEVICE);
  CHECK(ev.getBufferName().empty());
}

TEST(controller_midi_choice_is_saved_without_an_audio_event) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  auto path = scratchPath("controller_midi.conf");
  controller.setDeviceSettings(DeviceSettings(), path);

  CHECK(controller.setMidiInput("Keys:Keys MIDI 1").applied);
  CHECK(loadDeviceSettings(path).midi_input == "Keys:Keys MIDI 1");
  CHECK(!controller.getPlaybackEventQueue().hasEvents()); // the UI thread connects it
}

TEST(controller_refuses_a_capture_change_while_threshold_armed) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  controller.switchToBuffer(controller.freshBufferName());
  controller.setDeviceSettings(DeviceSettings{"pw:in", "", ""}, scratchPath("controller_refuse.conf"));
  controller.armThresholdRecording(0);
  CHECK(controller.isThresholdArmed());
  while (controller.getPlaybackEventQueue().hasEvents()) controller.getPlaybackEventQueue().pop();

  auto change = controller.setCaptureDevice("pw:other");
  CHECK(!change.applied);
  CHECK(!change.message.empty());
  CHECK(controller.getDeviceSettings().capture == "pw:in"); // nothing changed
  CHECK(!controller.getPlaybackEventQueue().hasEvents());
}

TEST(controller_reports_a_failed_save_but_still_applies_the_choice) {
  ChannelConfiguration config(44100, 1);
  Controller controller(config);
  // A directory where the file should go: the rename can't succeed.
  auto blocker = scratchPath("blocked.conf");
  std::filesystem::create_directories(blocker);
  controller.setDeviceSettings(DeviceSettings(), blocker);

  auto change = controller.setPlaybackDevice("pw:out");
  CHECK(change.applied);
  CHECK(!change.message.empty());
  CHECK(controller.getDeviceSettings().playback == "pw:out");
  std::filesystem::remove_all(blocker);
}
