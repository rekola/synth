#ifndef _STDERRCAPTURE_H_
#define _STDERRCAPTURE_H_

#include "Notifier.h"

#include <memory>
#include <string>

// Windows only: takes over the process's stderr (descriptor 2) so what is
// written to it can be shown on the UI's status line. A thread reads the pipe
// and raises notifier(); take() hands over what has come. When stderr was
// already a file or a pipe of the caller's, the bytes are also written to it.
// There is no waiting on an anonymous pipe, hence the thread.
class StderrCapture {
 public:
  StderrCapture();
  ~StderrCapture();
  StderrCapture(const StderrCapture &) = delete;
  StderrCapture & operator=(const StderrCapture &) = delete;

  // Whether stderr was taken over.
  bool active() const;
  Notifier & notifier();
  // The bytes that have arrived since the last call; clears notifier().
  std::string take();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

#endif
