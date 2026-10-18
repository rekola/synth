#ifndef _WAITSET_H_
#define _WAITSET_H_

#include "Notifier.h"

#include <vector>

#ifndef _WIN32
#include <poll.h>
#endif

// What a loop that waits on several sources at once blocks on: Notifiers and,
// on POSIX, file descriptors. Sources are added to groups; a loop adds the
// groups in the order it handles them and asks which are ready after wait().
// Built before the loop starts, not changed while another thread waits on it.
class WaitSet {
 public:
  using Group = int;

  Group group() { return next_group_++; }

  void add(Group group, Notifier & notifier);
#ifndef _WIN32
  void addFd(Group group, int fd);
  // The descriptors of a group, for a loop that waits on them itself.
  std::vector<int> fds(Group group) const;
#endif

  // Blocks up to timeout_ms (negative: for as long as it takes) for any
  // source; false if none became ready (or a signal interrupted the wait).
  bool wait(int timeout_ms);

  // Whether any source of the group was ready in the last wait().
  bool ready(Group group) const;

 private:
  struct Slot {
#ifdef _WIN32
    void * handle;
#else
    int fd;
#endif
    Group group;
    bool ready = false;
  };

  std::vector<Slot> slots_;
  Group next_group_ = 0;
#ifndef _WIN32
  std::vector<pollfd> fds_;
#endif
};

#endif
