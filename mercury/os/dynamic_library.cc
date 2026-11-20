// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dynamic_library.h"

#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Mercury::os {

namespace {

std::string PlatformError() {
#if defined(_WIN32)
  const DWORD code = GetLastError();
  char* message = nullptr;
  const DWORD size = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<char*>(&message), 0, nullptr);
  std::string text = size && message ? std::string(message, size) : "";
  if (message) LocalFree(message);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  return text.empty() ? "error " + std::to_string(code) : text;
#else
  const char* error = dlerror();
  return error ? error : "unknown dynamic loader error";
#endif
}

}  // namespace

DynamicLibrary::~DynamicLibrary() { Unload(); }

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(other.handle_), path_(std::move(other.path_)) {
  other.handle_ = nullptr;
  other.path_.clear();
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    Unload();
    handle_ = other.handle_;
    path_ = std::move(other.path_);
    other.handle_ = nullptr;
    other.path_.clear();
  }
  return *this;
}

bool DynamicLibrary::Load(const std::filesystem::path& path,
                          std::string* error) {
  Unload();
#if defined(_WIN32)
  handle_ = LoadLibraryW(path.wstring().c_str());
#else
  // RTLD_LOCAL keeps a module's symbols out of the process-wide namespace;
  // RTLD_NOW reports missing engine/wx symbols at load time instead of on the
  // first call.
  handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
  if (!handle_) {
    if (error) *error = path.string() + ": " + PlatformError();
    return false;
  }
  path_ = path;
  return true;
}

void* DynamicLibrary::GetSymbol(const char* name, std::string* error) const {
  if (!handle_) {
    if (error) *error = "the library is not loaded";
    return nullptr;
  }
#if defined(_WIN32)
  void* symbol = reinterpret_cast<void*>(
      GetProcAddress(static_cast<HMODULE>(handle_), name));
  if (!symbol && error) *error = std::string(name) + ": " + PlatformError();
#else
  dlerror();  // clear a previous error
  void* symbol = dlsym(handle_, name);
  if (!symbol && error) {
    *error = std::string(name) + ": " + PlatformError();
  }
#endif
  return symbol;
}

void DynamicLibrary::Unload() {
  if (!handle_) return;
#if defined(_WIN32)
  FreeLibrary(static_cast<HMODULE>(handle_));
#else
  dlclose(handle_);
#endif
  handle_ = nullptr;
  path_.clear();
}

}  // namespace Mercury::os
