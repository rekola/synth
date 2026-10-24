#pragma once

#ifdef _WIN32
#include <cellui/win32/Win32EventLoop.h>
#else
#include <cellui/PollEventLoop.h>
#endif

namespace cellui {

// The EventLoop of the platform for a program with no window: it waits on
// descriptors (poll) or, on Windows, on handles. A window's loop comes from
// its Backend instead.
#ifdef _WIN32
using PlatformEventLoop = win32::Win32EventLoop;
#else
using PlatformEventLoop = PollEventLoop;
#endif

}  // namespace cellui
