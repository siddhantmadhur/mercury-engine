#include "process.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

#include <cerrno>
#include <cstring>

namespace Mercury::os {

Process::~Process() {
  Kill();
  if (reader_.joinable()) reader_.join();
#if defined(_WIN32)
  if (read_pipe_) CloseHandle(read_pipe_);
  if (process_) CloseHandle(process_);
#else
  if (read_fd_ >= 0) close(read_fd_);
#endif
}

void Process::Append(const char* data, size_t size) {
  std::lock_guard lock(mutex_);
  for (size_t i = 0; i < size; i++) {
    char c = data[i];
    if (c == '\n') {
      if (!partial_line_.empty() && partial_line_.back() == '\r') {
        partial_line_.pop_back();
      }
      lines_.push_back(std::move(partial_line_));
      partial_line_.clear();
    } else {
      partial_line_.push_back(c);
    }
  }
}

std::vector<std::string> Process::TakeOutput() {
  std::lock_guard lock(mutex_);
  std::vector<std::string> out;
  out.swap(lines_);
  // Once the child is gone, a last line without '\n' is complete too.
  if (!running_ && !partial_line_.empty() && !reader_.joinable()) {
    out.push_back(std::move(partial_line_));
    partial_line_.clear();
  }
  return out;
}

std::optional<int> Process::GetExitCode() {
  IsRunning();
  return exit_code_;
}

#if defined(_WIN32)

namespace {
std::string QuoteArgument(const std::string& arg) {
  if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) {
    return arg;
  }
  std::string out = "\"";
  size_t backslashes = 0;
  for (char c : arg) {
    if (c == '\\') {
      backslashes++;
    } else if (c == '"') {
      out.append(backslashes * 2 + 1, '\\');
      backslashes = 0;
    } else {
      backslashes = 0;
    }
    out.push_back(c);
  }
  out.append(backslashes, '\\');
  return out + "\"";
}
}  // namespace

bool Process::Start(const std::vector<std::string>& args,
                    const std::filesystem::path& working_dir,
                    std::string* error) {
  if (running_ || args.empty()) return false;
  SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
    if (error) *error = "CreatePipe failed";
    return false;
  }
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

  std::string command_line;
  for (const auto& arg : args) {
    if (!command_line.empty()) command_line += ' ';
    command_line += QuoteArgument(arg);
  }

  STARTUPINFOA startup = {sizeof(startup)};
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = write_pipe;
  startup.hStdError = write_pipe;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION info = {};
  const std::string dir = working_dir.string();
  const BOOL ok = CreateProcessA(
      nullptr, command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
      nullptr, dir.empty() ? nullptr : dir.c_str(), &startup, &info);
  CloseHandle(write_pipe);
  if (!ok) {
    CloseHandle(read_pipe);
    if (error) *error = "could not start " + args[0];
    return false;
  }
  CloseHandle(info.hThread);
  process_ = info.hProcess;
  read_pipe_ = read_pipe;
  running_ = true;
  exit_code_.reset();
  reader_ = std::thread([this] { ReadLoop(); });
  return true;
}

void Process::ReadLoop() {
  char buffer[4096];
  DWORD read = 0;
  while (ReadFile(read_pipe_, buffer, sizeof(buffer), &read, nullptr) &&
         read > 0) {
    Append(buffer, read);
  }
}

void Process::Reap(bool block) {
  if (!running_ || !process_) return;
  if (WaitForSingleObject(process_, block ? INFINITE : 0) != WAIT_OBJECT_0) {
    return;
  }
  DWORD code = 0;
  GetExitCodeProcess(process_, &code);
  exit_code_ = static_cast<int>(code);
  running_ = false;
  if (reader_.joinable()) reader_.join();
}

bool Process::IsRunning() {
  Reap(false);
  return running_;
}

void Process::Kill() {
  if (!running_ || !process_) return;
  TerminateProcess(process_, 1);
  Reap(true);
}

bool LaunchDetached(const std::vector<std::string>& args, std::string* error) {
  if (args.empty()) return false;
  std::string command_line;
  for (const auto& arg : args) {
    if (!command_line.empty()) command_line += ' ';
    command_line += QuoteArgument(arg);
  }
  STARTUPINFOA startup = {sizeof(startup)};
  PROCESS_INFORMATION info = {};
  if (!CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                      DETACHED_PROCESS, nullptr, nullptr, &startup, &info)) {
    if (error) *error = "could not start " + args[0];
    return false;
  }
  CloseHandle(info.hThread);
  CloseHandle(info.hProcess);
  return true;
}

bool OpenWithDefaultApp(const std::filesystem::path& path) {
  auto result = ShellExecuteW(nullptr, L"open", path.wstring().c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<intptr_t>(result) > 32;
}

#else  // POSIX

bool Process::Start(const std::vector<std::string>& args,
                    const std::filesystem::path& working_dir,
                    std::string* error) {
  if (running_ || args.empty()) return false;
  int fds[2];
  if (pipe(fds) != 0) {
    if (error) *error = std::string("pipe: ") + std::strerror(errno);
    return false;
  }
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, fds[0]);
  posix_spawn_file_actions_addclose(&actions, fds[1]);
#if defined(__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 29)
  if (!working_dir.empty()) {
    posix_spawn_file_actions_addchdir_np(&actions, working_dir.c_str());
  }
#endif

  // Own process group, so Kill() also stops whatever the child starts
  // (cmake -> ninja -> compilers).
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&attributes, 0);

  std::vector<char*> argv;
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);

  pid_t pid = -1;
  const int result = posix_spawnp(&pid, argv[0], &actions, &attributes,
                                  argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attributes);
  close(fds[1]);
  if (result != 0) {
    close(fds[0]);
    if (error) *error = "could not start " + args[0] + ": " + std::strerror(result);
    return false;
  }

  pid_ = pid;
  read_fd_ = fds[0];
  running_ = true;
  exit_code_.reset();
  reader_ = std::thread([this] { ReadLoop(); });
  return true;
}

void Process::ReadLoop() {
  char buffer[4096];
  while (true) {
    ssize_t n = read(read_fd_, buffer, sizeof(buffer));
    if (n > 0) {
      Append(buffer, static_cast<size_t>(n));
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
}

void Process::Reap(bool block) {
  if (!running_ || pid_ <= 0) return;
  int status = 0;
  pid_t done = waitpid(pid_, &status, block ? 0 : WNOHANG);
  if (done != pid_) return;
  if (WIFEXITED(status)) {
    exit_code_ = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    exit_code_ = 128 + WTERMSIG(status);
  } else {
    exit_code_ = -1;
  }
  running_ = false;
  pid_ = -1;
  // The pipe closes once every process in the group has exited.
  if (reader_.joinable()) reader_.join();
  if (read_fd_ >= 0) {
    close(read_fd_);
    read_fd_ = -1;
  }
}

bool Process::IsRunning() {
  Reap(false);
  return running_;
}

void Process::Kill() {
  if (!running_ || pid_ <= 0) return;
  kill(-pid_, SIGTERM);
  // Give it a moment to exit cleanly, then force it.
  for (int i = 0; i < 50 && running_; i++) {
    Reap(false);
    if (running_) usleep(10'000);
  }
  if (running_) {
    kill(-pid_, SIGKILL);
    Reap(true);
  }
}

bool LaunchDetached(const std::vector<std::string>& args, std::string* error) {
  if (args.empty()) return false;
  std::vector<char*> argv;
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  // A new session, so closing the editor's terminal does not end it.
#if defined(POSIX_SPAWN_SETSID)
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID);
#else
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&attributes, 0);
#endif
  pid_t pid = -1;
  const int result =
      posix_spawnp(&pid, argv[0], nullptr, &attributes, argv.data(), environ);
  posix_spawnattr_destroy(&attributes);
  if (result != 0) {
    if (error) *error = "could not start " + args[0] + ": " + std::strerror(result);
    return false;
  }
  // Reap it whenever it exits so it does not linger as a zombie.
  std::thread([pid] {
    int status = 0;
    waitpid(pid, &status, 0);
  }).detach();
  return true;
}

bool OpenWithDefaultApp(const std::filesystem::path& path) {
#if defined(__APPLE__)
  return LaunchDetached({"open", path.string()});
#else
  return LaunchDetached({"xdg-open", path.string()});
#endif
}

#endif

}  // namespace Mercury::os
