#include "WinMmDevices.h"

#include "../util/WinSupport.h"

#include <mmsystem.h>

#include <chrono>

using namespace std;

namespace winmm {

vector<Device>
inputs() {
  vector<Device> result;
  for (UINT i = 0, n = midiInGetNumDevs(); i < n; i++) {
    MIDIINCAPSW caps;
    if (midiInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
    result.push_back({i, winsupport::toUtf8(caps.szPname)});
  }
  return result;
}

vector<Device>
outputs() {
  vector<Device> result;
  for (UINT i = 0, n = midiOutGetNumDevs(); i < n; i++) {
    MIDIOUTCAPSW caps;
    if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
    result.push_back({i, winsupport::toUtf8(caps.szPname)});
  }
  return result;
}

Short
unpack(unsigned long long word) {
  Short message;
  message.status = static_cast<int>(word & 0xFF);
  message.channel_status = message.status & 0xF0;
  message.data1 = static_cast<int>((word >> 8) & 0x7F);
  message.data2 = static_cast<int>((word >> 16) & 0x7F);
  return message;
}

string
Watcher::signature() {
  string result;
  for (auto & d : inputs()) result += "i" + to_string(d.id) + d.name + "\n";
  for (auto & d : outputs()) result += "o" + to_string(d.id) + d.name + "\n";
  return result;
}

Watcher::Watcher(Notifier & notifier) : notifier_(notifier), last_(signature()) {
  thread_ = thread([this]() {
    while (!stop_) {
      // Short sleeps, so a destructor never waits long.
      for (int i = 0; i < 10 && !stop_; i++) this_thread::sleep_for(chrono::milliseconds(100));
      if (stop_) break;
      auto now = signature();
      if (now == last_) continue;
      last_ = now;
      changed_ = true;
      notifier_.signal();
    }
  });
}

Watcher::~Watcher() {
  stop_ = true;
  thread_.join();
}

} // namespace winmm
