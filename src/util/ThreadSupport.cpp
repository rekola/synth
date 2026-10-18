#include "ThreadSupport.h"

#ifdef _WIN32
#include <windows.h>
#include <avrt.h>

#include <string>
#else
#include <pthread.h>
#endif

void
setCurrentThreadName(const char * name) {
#ifdef _WIN32
  // SetThreadDescription needs Windows 10 1607; older systems just go unnamed.
  using SetDescription = HRESULT(WINAPI *)(HANDLE, PCWSTR);
  auto set = reinterpret_cast<SetDescription>(reinterpret_cast<void (*)()>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
  if (!set) return;
  std::wstring wide;
  for (const char * c = name; *c; c++) wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*c)));
  set(GetCurrentThread(), wide.c_str());
#elif defined(__APPLE__)
  // Names the calling thread only.
  pthread_setname_np(name);
#else
  pthread_setname_np(pthread_self(), name);
#endif
}

#ifdef _WIN32
AudioThreadPriority::AudioThreadPriority() {
  // Multimedia Class Scheduler: the "Pro Audio" task keeps the thread ahead
  // of ordinary work for the short deadlines of an audio callback.
  DWORD task_index = 0;
  task_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
}

AudioThreadPriority::~AudioThreadPriority() {
  if (task_) AvRevertMmThreadCharacteristics(task_);
}
#else
AudioThreadPriority::AudioThreadPriority() { }
AudioThreadPriority::~AudioThreadPriority() { }
#endif
