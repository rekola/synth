#include "TestFramework.h"

#include "../src/audio/DeviceLabels.h"

TEST(device_detail_cuts_a_usb_bus_path_down_to_the_port) {
  CHECK(deviceDetail("alsa_input.usb-X-00.analog-stereo", "pci-0000:00:14.0-usb-0:2:1.0") == "usb-0:2:1.0");
  CHECK(deviceDetail("ignored", "pci-0000:00:1f.3") == "pci-0000:00:1f.3"); // not USB: kept whole
}

TEST(device_detail_without_a_bus_path_comes_from_the_node_name) {
  CHECK(deviceDetail("alsa_input.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-stereo", "") ==
        "usb-Burr-Brown_from_TI_USB_Audio_CODEC-00");
  CHECK(deviceDetail("alsa_output.pci-0000_00_1f.3.hdmi-stereo", "") == "pci-0000_00_1f.3");
  CHECK(deviceDetail("bluez_output.AA_BB_CC", "") == "bluez_output.AA_BB_CC"); // not an ALSA name: as is
  CHECK(deviceDetail("alsa_input.nodots", "") == "nodots");
}

TEST(disambiguate_leaves_unique_labels_alone) {
  std::vector<std::string> labels = {"Mic", "Line In"};
  disambiguateLabels(labels, {"usb-0:1", "usb-0:2"});
  CHECK(labels[0] == "Mic");
  CHECK(labels[1] == "Line In");
}

TEST(disambiguate_tells_identical_devices_apart_by_detail) {
  std::vector<std::string> labels = {"USB Audio CODEC", "Other", "USB Audio CODEC"};
  disambiguateLabels(labels, {"usb-0:2:1.0", "x", "usb-0:3:1.0"});
  CHECK(labels[0] == "USB Audio CODEC [usb-0:2:1.0]");
  CHECK(labels[1] == "Other");
  CHECK(labels[2] == "USB Audio CODEC [usb-0:3:1.0]");
}

TEST(disambiguate_numbers_what_the_details_cannot_separate) {
  std::vector<std::string> labels = {"Mic", "Mic", "Mic"};
  disambiguateLabels(labels, {"same", "same", "other"});
  // The two with equal detail stay equal and get numbered; the third is told apart.
  CHECK(labels[0] == "Mic");
  CHECK(labels[1] == "Mic (2)");
  CHECK(labels[2] == "Mic [other]");
}

TEST(disambiguate_without_details_just_numbers) {
  std::vector<std::string> labels = {"Mic", "Mic"};
  disambiguateLabels(labels, {});
  CHECK(labels[0] == "Mic");
  CHECK(labels[1] == "Mic (2)");
}

TEST(disambiguate_result_has_no_duplicates) {
  std::vector<std::string> labels = {"A", "A", "A [x]", "A", "A [x]"};
  disambiguateLabels(labels, {"x", "", "", "x", ""});
  for (size_t i = 0; i < labels.size(); i++)
    for (size_t j = i + 1; j < labels.size(); j++) CHECK(labels[i] != labels[j]);
}
