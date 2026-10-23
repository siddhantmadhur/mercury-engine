/*
 * lore_vcs.h
 *
 * The Lore panel: version control for the open project with Epic's Lore,
 * driven through the `lore` command line tool run in the project folder.
 * Only the game project is versioned with Lore; the engine itself uses git.
 *
 *   no .lore folder   only "Initialize Repository": creates the repository on
 *                     the server and writes a .loreignore for generated files
 *   Changes           staged and unstaged files: stage, unstage, discard,
 *                     lock, diff, and the commit message box
 *   History           revisions; the latest one's message can be edited until
 *                     it is pushed, and any revision can be synced to
 *   Locks             files locked on the branch and who holds them
 *
 * The toolbar switches and creates branches, syncs and pushes. Options (the
 * gear) sets the server the repository talks to (remote_url in
 * .lore/config.toml) and which lore executable to run.
 *
 * Commands run one at a time in the background; what they print goes to the
 * Console under [lore].
 */
#ifndef MERCURY_EDITOR_LORE_VCS_H
#define MERCURY_EDITOR_LORE_VCS_H

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

  LoreVcs(Console& console, EditorSettings& settings);
  ~LoreVcs();

  void SetProject(std::shared_ptr<Project> project);

  // Call every frame: runs queued commands and handles their results.
  void Update();
  void Draw(bool* open);

  // True once the project folder holds a Lore repository.
  bool IsRepository() const;

  // For the Project panel. `path` is absolute and inside the project.
  bool IsLocked(const std::filesystem::path& path) const;
  void Lock(const std::filesystem::path& path);
  void Unlock(const std::filesystem::path& path);

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

  // ------------------------------------------------------------- commands
  void Run(std::vector<std::string> args, Callback done = {}, bool quiet = false);
  void StartNext();
  void Finish();
  // Queues status, locks, history, branches and repository info.
  void Refresh();

  void Initialize();
  void Stage(const std::vector<std::string>& paths);
  void Unstage(const std::vector<std::string>& paths);
  void Discard(const Change& change);
  void Commit(bool then_push);
  void Push();
  void Sync(const std::string& revision = {});
  void SwitchBranch(const std::string& name);
  void CreateBranch(const std::string& name);
  void AmendMessage(const std::string& message);
  void ShowDiff(const std::string& path);
  void ShowRevision(const Revision& revision);
  void LockPaths(const std::vector<std::string>& paths, bool lock);

  void ParseStatus(const std::vector<std::string>& lines);
  void ParseHistory(const std::vector<std::string>& lines);
  void ParseLocks(const std::vector<std::string>& lines);
  void ParseBranches(const std::vector<std::string>& lines);
  void ParseInfo(const std::vector<std::string>& lines);

  // remote_url in .lore/config.toml
  std::string ReadRemoteUrl() const;
  bool WriteRemoteUrl(const std::string& url);
  std::string RelativePath(const std::filesystem::path& path) const;
  bool HasUnpushedCommit() const;

  // ---------------------------------------------------------------- panel
  void DrawInit();
  void DrawToolbar();
  void DrawOptions();
  void DrawChanges();
  void DrawChangeList(Section section, const char* id);
  void DrawChangeMenu(const Change& change);
  void DrawCommitBox();
  void DrawHistory();
  void DrawLocks();
  void DrawModals();
  void DrawTextLines(const std::vector<std::string>& lines, bool diff);

  Console& console_;
  EditorSettings& settings_;
  std::shared_ptr<Project> project_;

  std::deque<Job> queue_;
  std::unique_ptr<os::Process> process_;
  Job current_;
  std::vector<std::string> output_;
  bool refresh_queued_ = false;
  bool was_focused_ = false;

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

  // Panel state
  std::string commit_message_;
  std::string selected_path_;  // change shown in the diff view
  std::vector<std::string> diff_lines_;
  int selected_revision_ = -1;
  std::vector<std::string> revision_lines_;
  std::string init_server_;
  std::string init_name_;
  std::string options_server_;
  std::string options_executable_;
  std::string amend_message_;
  std::string new_branch_;
  std::string lock_path_;
  Change discard_target_;
  enum class Modal { kNone, kAmend, kNewBranch, kDiscard };
  Modal modal_ = Modal::kNone;
  bool open_modal_ = false;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_LORE_VCS_H
