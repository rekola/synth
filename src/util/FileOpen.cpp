#include "FileOpen.h"

#ifdef _WIN32
#include <windows.h>

namespace {

std::wstring
utf8ToWide(const std::string & s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  w.resize(static_cast<size_t>(n) - 1);
  return w;
}

} // namespace

FILE *
openFile(const std::string & path, const char * mode) {
  return _wfopen(utf8ToWide(path).c_str(), utf8ToWide(mode).c_str());
}

SNDFILE *
openSoundFile(const std::string & path, int mode, SF_INFO * info) {
  return sf_wchar_open(utf8ToWide(path).c_str(), mode, info);
}
#else
FILE *
openFile(const std::string & path, const char * mode) {
  return std::fopen(path.c_str(), mode);
}

SNDFILE *
openSoundFile(const std::string & path, int mode, SF_INFO * info) {
  return sf_open(path.c_str(), mode, info);
}
#endif
