#ifndef _WINSUPPORT_H_
#define _WINSUPPORT_H_

// Helpers for the Windows backends (audio endpoints, MIDI). Includes
// <windows.h>, so only .cpp files of those backends include it.

#include <windows.h>

#include <string>

namespace winsupport {

inline std::string toUtf8(const wchar_t * text) {
  if (!text || !*text) return std::string();
  int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return std::string();
  std::string result(static_cast<size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
  return result;
}

inline std::wstring toWide(const std::string & text) {
  if (text.empty()) return std::wstring();
  int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (size <= 1) return std::wstring();
  std::wstring result(static_cast<size_t>(size - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
  return result;
}

// An owning pointer to a COM object.
template <class T>
class ComPtr {
 public:
  ComPtr() = default;
  ComPtr(const ComPtr &) = delete;
  ComPtr & operator=(const ComPtr &) = delete;
  ComPtr(ComPtr && other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
  ComPtr & operator=(ComPtr && other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      other.ptr_ = nullptr;
    }
    return *this;
  }
  ~ComPtr() { reset(); }

  void reset() {
    if (ptr_) ptr_->Release();
    ptr_ = nullptr;
  }
  T * get() const { return ptr_; }
  T * operator->() const { return ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }
  // For a function that returns a new object through an out-parameter.
  T ** put() {
    reset();
    return &ptr_;
  }
  void ** putVoid() { return reinterpret_cast<void **>(put()); }

 private:
  T * ptr_ = nullptr;
};

// Joins the multithreaded COM apartment for the calling thread, once; the
// thread leaves it when it ends. Cheap to call again.
void ensureComInitialized();

} // namespace winsupport

#endif
