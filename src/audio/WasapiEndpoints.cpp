#include <initguid.h>

#include "WasapiEndpoints.h"
#include "DeviceSettings.h"

#include <functiondiscoverykeys_devpkey.h>

using namespace std;
using winsupport::ComPtr;

namespace {

ComPtr<IMMDeviceEnumerator>
createEnumerator() {
  winsupport::ensureComInitialized();
  ComPtr<IMMDeviceEnumerator> enumerator;
  CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), enumerator.putVoid());
  return enumerator;
}

string
idOf(IMMDevice * device) {
  LPWSTR id = nullptr;
  string result;
  if (SUCCEEDED(device->GetId(&id)) && id) result = winsupport::toUtf8(id);
  if (id) CoTaskMemFree(id);
  return result;
}

string
friendlyName(IMMDevice * device) {
  ComPtr<IPropertyStore> store;
  if (FAILED(device->OpenPropertyStore(STGM_READ, store.put()))) return string();
  PROPVARIANT value;
  PropVariantInit(&value);
  string result;
  if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR) {
    result = winsupport::toUtf8(value.pwszVal);
  }
  PropVariantClear(&value);
  return result;
}

} // namespace

namespace wasapi {

vector<Endpoint>
listEndpoints(bool capture) {
  vector<Endpoint> result;
  auto enumerator = createEnumerator();
  if (!enumerator) return result;
  ComPtr<IMMDeviceCollection> devices;
  if (FAILED(enumerator->EnumAudioEndpoints(capture ? eCapture : eRender, DEVICE_STATE_ACTIVE, devices.put()))) return result;
  UINT count = 0;
  devices->GetCount(&count);
  for (UINT i = 0; i < count; i++) {
    ComPtr<IMMDevice> device;
    if (FAILED(devices->Item(i, device.put()))) continue;
    auto id = idOf(device.get());
    if (id.empty()) continue;
    auto name = friendlyName(device.get());
    result.push_back({id, name.empty() ? id : name});
  }
  return result;
}

ComPtr<IMMDevice>
openEndpoint(const string & name, bool capture, string & error) {
  ComPtr<IMMDevice> device;
  auto enumerator = createEnumerator();
  if (!enumerator) {
    error = "Windows audio is not available";
    return device;
  }
  if (isDefaultDevice(name)) {
    if (FAILED(enumerator->GetDefaultAudioEndpoint(capture ? eCapture : eRender, eConsole, device.put()))) {
      device.reset();
      error = capture ? "no default audio input" : "no default audio output";
    }
    return device;
  }
  if (!isWasapiDevice(name)) {
    error = "'" + name + "' is not a Windows audio device name";
    return device;
  }
  if (FAILED(enumerator->GetDevice(winsupport::toWide(wasapiEndpointId(name)).c_str(), device.put()))) {
    device.reset();
    error = "device not found";
    return device;
  }
  DWORD state = 0;
  if (FAILED(device->GetState(&state)) || state != DEVICE_STATE_ACTIVE) {
    device.reset();
    error = "device is not active";
  }
  return device;
}

} // namespace wasapi

namespace winsupport {

void
ensureComInitialized() {
  struct Apartment {
    bool joined;
    Apartment() : joined(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) { }
    ~Apartment() {
      if (joined) CoUninitialize();
    }
  };
  thread_local Apartment apartment;
}

} // namespace winsupport
