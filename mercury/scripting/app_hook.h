// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * app_hook.h
 *
 * Process-wide entry points for C++ game code: read the command line before
 * the game starts, initialize something that must outlive the scene (a network
 * socket, a service, a log sink), and shut it down when the game exits.
 *
 * Hooks are registered with MERCURY_APP_HOOK(Type), the same static-init
 * pattern MERCURY_SCRIPT and MERCURY_SYSTEM use, and are called by
 * Mercury::RunPlayer (player/player.cc). The editor's script-build and
 * `--dump-schema` runs never invoke them.
 */
#ifndef MERCURY_SCRIPTING_APP_HOOK_H
#define MERCURY_SCRIPTING_APP_HOOK_H

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Mercury {

/**
 * @brief Process-wide callbacks around a game run
 *
 * A hook is created once by @ref RunPlayer. Keep state in the hook object (it
 * lives until the process is done playing); `OnPreInit` is the only callback
 * that sees the command line, so remember what it parsed for later callbacks.
 *
 * The callbacks run in registration order:
 * 1. `OnPreInit(args)` before the application and scene exist.
 * 2. `OnInit()` after the project and scripts are loaded, before the window.
 * 3. `OnShutdown()`, in reverse registration order, after the scene is gone.
 *
 * @par Example
 * @code{.cpp}
 * class ServerHook : public Mercury::AppHook {
 *  public:
 *   bool OnPreInit(std::span<const std::string_view> args) override {
 *     for (const std::string_view arg : args) {
 *       if (arg == "--server") server_ = true;
 *     }
 *     return true;
 *   }
 *   bool OnInit() override {
 *     if (server_ && !listener_.Listen(7777)) return false;
 *     return true;
 *   }
 *   void OnShutdown() override { listener_.Close(); }
 *  private:
 *   bool server_ = false;
 *   Listener listener_;
 * };
 *
 * MERCURY_APP_HOOK(ServerHook);
 * @endcode
 */
class AppHook {
 public:
  virtual ~AppHook() = default;

  // The program's arguments, without argv[0]. Returning false stops the run
  // (RunPlayer returns 1) after every hook's OnShutdown has been called.
  virtual bool OnPreInit(std::span<const std::string_view> args) {
    (void)args;
    return true;
  }
  // The project and its scripts are loaded; the application is not created
  // yet. Returning false stops the run like OnPreInit.
  virtual bool OnInit() { return true; }
  // The scene has been destroyed; release process-wide resources here.
  virtual void OnShutdown() {}
};

/**
 * @brief One registered AppHook type, as stored by AppHookRegistry
 */
struct AppHookInfo {
  std::string name;
  std::function<std::unique_ptr<AppHook>()> create;
};

/**
 * @brief The process-wide list of registered AppHook types
 *
 * Like SystemRegistry, registration is normally a static initializer
 * (MERCURY_APP_HOOK), so the list is complete before RunPlayer starts.
 */
class AppHookRegistry {
 public:
  static AppHookRegistry& Instance();

  // Registering the same name again replaces the earlier entry.
  void Register(AppHookInfo info);
  const std::vector<AppHookInfo>& Hooks() const { return hooks_; }
  // Creates one instance of every registered hook, in registration order.
  std::vector<std::unique_ptr<AppHook>> CreateAll() const;
  // Removes every registration (tests).
  void Clear();

 private:
  AppHookRegistry() = default;

  std::vector<AppHookInfo> hooks_;
};

/**
 * @brief Register an AppHook subclass
 *
 * Use through MERCURY_APP_HOOK(Type) in the hook's .cc file. The type must
 * derive from @ref AppHook and be default-constructible.
 *
 * @return true, so the macro can initialize a variable with it.
 */
template <typename T>
bool RegisterAppHook(const char* name) {
  static_assert(std::is_base_of_v<AppHook, T>,
                "MERCURY_APP_HOOK types must derive from Mercury::AppHook");
  static_assert(std::is_default_constructible_v<T>,
                "MERCURY_APP_HOOK types must be default-constructible");
  AppHookInfo info;
  info.name = name;
  info.create = [] { return std::unique_ptr<AppHook>(new T()); };
  AppHookRegistry::Instance().Register(std::move(info));
  return true;
}

}  // namespace Mercury

/**
 * @brief Register a process-wide app hook
 * @param Type A class deriving from Mercury::AppHook
 *
 * Put this in the hook's .cc file. It runs at static initialization time.
 */
#define MERCURY_APP_HOOK(Type)                                    \
  [[maybe_unused]] static const bool mercury_app_hook_##Type##_ = \
      ::Mercury::RegisterAppHook<Type>(#Type)

#endif  // MERCURY_SCRIPTING_APP_HOOK_H
