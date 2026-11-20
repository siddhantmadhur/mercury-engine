// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * dynamic_library.h
 *
 * Small RAII wrapper around the platform's dynamic loader (dlopen on macOS and
 * Linux, LoadLibrary on Windows). The editor uses it to load its `system/`
 * modules; nothing else in the engine links code at run time.
 */
#ifndef MERCURY_OS_DYNAMIC_LIBRARY_H
#define MERCURY_OS_DYNAMIC_LIBRARY_H

#include <filesystem>
#include <string>

namespace Mercury::os {

/**
 * @brief A loaded dynamic library (an .so, .dylib or .dll)
 *
 * Move-only. Unload() (or the destructor) frees it. Symbols are looked up by
 * their unmangled C name; use `extern "C"` for entry points.
 */
class DynamicLibrary {
 public:
  DynamicLibrary() = default;
  ~DynamicLibrary();
  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;
  DynamicLibrary(DynamicLibrary&& other) noexcept;
  DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

  // Loads the library. False with `error` filled when it cannot be opened.
  bool Load(const std::filesystem::path& path, std::string* error = nullptr);
  // The address of `name`, or null with `error` filled.
  void* GetSymbol(const char* name, std::string* error = nullptr) const;

  bool IsLoaded() const { return handle_ != nullptr; }
  void Unload();

  const std::filesystem::path& GetPath() const { return path_; }

 private:
  void* handle_ = nullptr;
  std::filesystem::path path_;
};

}  // namespace Mercury::os

#endif  // MERCURY_OS_DYNAMIC_LIBRARY_H
