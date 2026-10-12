#include "AudioDevices.h"

#include "DeviceLabels.h"
#include "DeviceSettings.h"
#include "WasapiEndpoints.h"

using namespace std;

namespace {

vector<AudioDeviceInfo>
listDevices(bool capture) {
  vector<AudioDeviceInfo> devices;
  devices.push_back({"", "System default", ""});
  for (auto & endpoint : wasapi::listEndpoints(capture)) {
    devices.push_back({string(kWasapiPrefix) + endpoint.id, endpoint.name, ""});
  }
  uniquifyLabels(devices);
  return devices;
}

bool
exists(const string & name, bool capture) {
  if (!isWasapiDevice(name)) return true;
  string error;
  return static_cast<bool>(wasapi::openEndpoint(name, capture, error));
}

} // namespace

bool
pipeWireAvailable() {
  return false;
}

vector<AudioDeviceInfo>
listCaptureDevices() {
  return listDevices(true);
}

vector<AudioDeviceInfo>
listPlaybackDevices() {
  return listDevices(false);
}

bool
captureDeviceExists(const string & name) {
  return exists(name, true);
}

bool
playbackDeviceExists(const string & name) {
  return exists(name, false);
}

void
uniquifyLabels(vector<AudioDeviceInfo> & devices) {
  vector<string> labels, details;
  for (auto & device : devices) {
    labels.push_back(device.label);
    details.push_back(device.detail);
  }
  disambiguateLabels(labels, details);
  for (size_t i = 0; i < devices.size(); i++) devices[i].label = labels[i];
}
