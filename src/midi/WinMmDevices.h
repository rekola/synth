#ifndef _WINMMDEVICES_H_
#define _WINMMDEVICES_H_

#include "../util/Notifier.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

// The MIDI devices WinMM offers, which has no notice of one coming or going.
namespace winmm {

struct Device {
  unsigned id = 0;
  std::string name;
};

std::vector<Device> inputs();
std::vector<Device> outputs();

// Short messages arrive packed in one word: status, data 1, data 2.
struct Short {
  int status = 0;
  int channel_status = 0;
  int data1 = 0;
  int data2 = 0;
};
Short unpack(unsigned long long word);

// Watches the device lists from a thread of its own and raises the notifier
// when they changed, so the owner re-reads them on its thread.
class Watcher {
 public:
  explicit Watcher(Notifier & notifier);
  ~Watcher();

  // Whether the lists changed since the last call.
  bool takeChanged() { return changed_.exchange(false); }

 private:
  static std::string signature();

  Notifier & notifier_;
  std::atomic<bool> stop_ { false };
  std::atomic<bool> changed_ { false };
  std::string last_;
  std::thread thread_;
};

} // namespace winmm

#endif
