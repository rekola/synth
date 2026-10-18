#ifndef _THREADSUPPORT_H_
#define _THREADSUPPORT_H_

// Names the calling thread for debuggers and process listings (top -H, ps -L
// show 15 characters at most).
void setCurrentThreadName(const char * name);

// Asks the system to schedule the calling thread for audio work for as long
// as the object lives. Where the system has no such facility, or refuses, it
// does nothing: the thread runs as it would have.
class AudioThreadPriority {
 public:
  AudioThreadPriority();
  ~AudioThreadPriority();

  AudioThreadPriority(const AudioThreadPriority &) = delete;
  AudioThreadPriority & operator=(const AudioThreadPriority &) = delete;

  // Whether the system granted it.
  bool granted() const { return task_ != nullptr; }

 private:
  void * task_ = nullptr;
};

#endif
