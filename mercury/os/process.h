/*
 * process.h
 *
 * Runs a child process with its stdout and stderr captured line by line. The
 * editor uses it to build the game with CMake and to run the player.
 */
#ifndef MERCURY_OS_PROCESS_H
#define MERCURY_OS_PROCESS_H

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Mercury::os {

class Process {
 public:
  Process() = default;
  ~Process();  // kills the child if it is still running
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;

  /**
   * @brief Start `args[0]` (searched on PATH) with the remaining arguments
   * @return false with `error` filled if it could not be started
   */
  bool Start(const std::vector<std::string>& args,
             const std::filesystem::path& working_dir = {},
             std::string* error = nullptr);

  // Checks (without blocking) whether the child has exited.
  bool IsRunning();
  // Exit code once the child has exited.
  std::optional<int> GetExitCode();

  // Ends the child and everything it started.
  void Kill();

  // Output lines (stdout and stderr interleaved) received since the last
  // call. Safe to call every frame.
  std::vector<std::string> TakeOutput();

 private:
  void ReadLoop();
  void Append(const char* data, size_t size);
  void Reap(bool block);

#if defined(_WIN32)
  void* process_ = nullptr;  // HANDLE
  void* read_pipe_ = nullptr;
#else
  int pid_ = -1;
  int read_fd_ = -1;
#endif
  bool running_ = false;
  std::optional<int> exit_code_;

  std::thread reader_;
  std::mutex mutex_;
  std::string partial_line_;
  std::vector<std::string> lines_;
};

// Starts a program that outlives this process (no output capture), e.g. the
// user's IDE.
bool LaunchDetached(const std::vector<std::string>& args,
                    std::string* error = nullptr);

// True if `program` (without .exe) is found in one of the PATH folders.
bool IsOnPath(const std::string& program);

// Opens a file or folder with the desktop's default application.
bool OpenWithDefaultApp(const std::filesystem::path& path);

}  // namespace Mercury::os

#endif  // MERCURY_OS_PROCESS_H
