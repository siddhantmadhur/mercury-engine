/*
 * lore_vcs.h
 *
 * Version control controller for the open project with Epic's Lore, driven
 * through the `lore` command line tool run in the project folder.
 * Only the game project is versioned with Lore; the engine itself uses git.
 *
 *   no .lore folder   InitializeRepository(server, name): creates the
 *                     repository on the server and writes a .loreignore
 *   Changes           staged and unstaged files: stage, unstage, discard,
 *                     lock, diff, and commit
 *   History           revisions; the latest one's message can be edited until
 *                     it is pushed, and any revision can be synced to
 *   Locks             files locked on the branch and who holds them
 *
 * Commands run one at a time in the background; what they print goes to the
 * Console under [lore].
 */
#ifndef MERCURY_EDITOR_LORE_VCS_H
#define MERCURY_EDITOR_LORE_VCS_H

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "console.h"
#include "editor_settings.h"
#include "os/process.h"
#include "project/project.h"

namespace Mercury {

class LoreVcs {
 public:
  static constexpr const char* kWindowName = "Lore";

  enum class Section { kStaged, kUnstaged, kUntracked };
  struct Change {
    char code = 'M';  // A added, M modified, D deleted, V moved
    std::string path;
    std::string from;  // for moves
    Section section = Section::kUnstaged;
  };
  struct Revision {
    int number = 0;
    std::string signature;
    std::string date;
    std::string message;
  };
  struct LockInfo {
    std::string owner;
    std::string branch;
  };

  LoreVcs(Console& console, EditorSettings& settings);
  ~LoreVcs();

  void SetProject(std::shared_ptr<Project> project);

  // Call every frame: runs queued commands and handles their results.
  void Update();

  // True once the project folder holds a Lore repository.
  bool IsRepository() const;
  bool HasProject() const { return project_ != nullptr; }
  std::shared_ptr<Project> GetProject() const { return project_; }

  // For the Project panel. `path` is absolute and inside the project.
  bool IsLocked(const std::filesystem::path& path) const;
  void Lock(const std::filesystem::path& path);
  void Unlock(const std::filesystem::path& path);

  // Monotonically increases whenever status/history/locks/diff/error change.
  uint64_t StateRevision() const { return state_revision_; }
  bool IsBusy() const { return process_ != nullptr; }
  bool IsLoaded() const { return loaded_; }
  std::string CurrentOperation() const {
    return current_.args.empty() ? "" : current_.args[0];
  }

  // Repository queries & state
  const std::string& Branch() const { return branch_; }
  int LocalRevision() const { return local_revision_; }
  int RemoteRevision() const { return remote_revision_; }
  const std::string& SyncState() const { return sync_state_; }
  const std::vector<Change>& Changes() const { return changes_; }
  const std::vector<Revision>& History() const { return history_; }
  const std::map<std::string, LockInfo>& Locks() const { return locks_; }
  const std::vector<std::string>& Branches() const { return branches_; }
  const std::vector<std::string>& RemoteBranches() const {
    return remote_branches_;
  }
  const std::string& RepositoryName() const { return repository_name_; }
  const std::string& LastError() const { return last_error_; }
  bool HasUnpushedCommit() const;
  bool IsAheadOfRemote() const;

  const std::string& SelectedPath() const { return selected_path_; }
  const std::vector<std::string>& DiffLines() const { return diff_lines_; }
  int SelectedRevision() const { return selected_revision_; }
  const std::vector<std::string>& RevisionLines() const {
    return revision_lines_;
  }

  std::string ReadRemoteUrl() const;
  bool WriteRemoteUrl(const std::string& url);
  EditorSettings& Settings() { return settings_; }
  Console& ConsoleLog() { return console_; }

  // Actions
  void Refresh();
  void Initialize(const std::string& server_url, const std::string& repo_name);
  void Stage(const std::vector<std::string>& paths);
  void StageAll();
  void Unstage(const std::vector<std::string>& paths);
  void Discard(const Change& change);
  void Commit(const std::string& message, bool then_push);
  void Push();
  void Sync(const std::string& revision = {});
  void SwitchBranch(const std::string& name);
  void CreateBranch(const std::string& name);
  void AmendMessage(const std::string& message);
  void ShowDiff(const std::string& path);
  void ShowRevision(const Revision& revision);
  void LockPaths(const std::vector<std::string>& paths, bool lock);
  void ClearError() {
    last_error_.clear();
    state_revision_++;
  }

 private:
  struct Result {
    int exit_code = -1;
    std::vector<std::string> lines;
    std::string error;  // the "[Error] ..." line, or why lore didn't start
    bool ok() const { return exit_code == 0 && error.empty(); }
  };
  using Callback = std::function<void(const Result&)>;
  struct Job {
    std::vector<std::string> args;  // after "lore"
    Callback done;
    // Queries (status, history, ...) only log failures; everything else
    // echoes its command and output to the Console.
    bool quiet = false;
  };

  void Run(std::vector<std::string> args, Callback done = {}, bool quiet = false);
  void StartNext();
  void Finish();

  void ParseStatus(const std::vector<std::string>& lines);
  void ParseHistory(const std::vector<std::string>& lines);
  void ParseLocks(const std::vector<std::string>& lines);
  void ParseBranches(const std::vector<std::string>& lines);
  void ParseInfo(const std::vector<std::string>& lines);

  std::string RelativePath(const std::filesystem::path& path) const;

  Console& console_;
  EditorSettings& settings_;
  std::shared_ptr<Project> project_;

  std::deque<Job> queue_;
  std::unique_ptr<os::Process> process_;
  Job current_;
  std::vector<std::string> output_;
  bool refresh_queued_ = false;
  uint64_t state_revision_ = 1;

  // What the last refresh found.
  bool loaded_ = false;
  std::string branch_;
  int local_revision_ = -1;
  int remote_revision_ = -1;  // -1: no remote branch or the server is offline
  std::string sync_state_;    // "Local branch is ahead of remote", ...
  std::vector<Change> changes_;
  std::vector<Revision> history_;
  std::map<std::string, LockInfo> locks_;  // project-relative path -> holder
  std::vector<std::string> branches_;
  std::vector<std::string> remote_branches_;
  std::string repository_name_;
  std::string last_error_;

  std::string selected_path_;  // change shown in the diff view
  std::vector<std::string> diff_lines_;
  int selected_revision_ = -1;
  std::vector<std::string> revision_lines_;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_LORE_VCS_H
