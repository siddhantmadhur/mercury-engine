// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * model_preloader.h
 *
 * Loads model files on background threads so a big map does not freeze the
 * editor or the game. Model::Load only produces CPU data (the renderer
 * uploads it to the GPU later, on the main thread), so a worker can run it;
 * the caller polls for progress and takes the finished models.
 *
 * Only the thread that calls Start() may use the other members. The workers
 * hold the load state themselves, so Cancel() can return without waiting for
 * an import that is already running.
 */
#ifndef MERCURY_SCENE_MODEL_PRELOADER_H
#define MERCURY_SCENE_MODEL_PRELOADER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "properties/model.h"

namespace Mercury {

class ModelPreloader {
 public:
  struct Job {
    std::string res_path;  // the key the model is cached under
    std::string file;      // the path Model::Load reads
    ModelImportSettings settings;
  };

  struct Result {
    std::string res_path;
    std::shared_ptr<const Model>
        model;  // null if the model could not be loaded
  };

  ModelPreloader() = default;
  ~ModelPreloader() { Cancel(); }
  ModelPreloader(const ModelPreloader&) = delete;
  ModelPreloader& operator=(const ModelPreloader&) = delete;

  // Starts loading; a load already in progress is cancelled first.
  void Start(std::vector<Job> jobs);

  // Stops the workers and drops every result. An import that is already
  // running cannot be interrupted inside Assimp; the workers finish it in the
  // background with the state they hold.
  void Cancel();

  // True from Start until TakeResults or Cancel.
  bool IsBusy() const { return active_; }
  // True once every job has run.
  bool IsFinished() const {
    return active_ && run_ && run_->done >= run_->jobs.size();
  }

  size_t Done() const { return run_ ? run_->done.load() : 0; }
  size_t Total() const { return run_ ? run_->jobs.size() : 0; }
  // How much of all the work is done (0-1), counting the models still loading
  // by how far along they are.
  float Fraction() const;
  // The model a worker started most recently, for a progress label.
  std::string CurrentName() const;

  // The results of a finished load, in job order. Ends the load.
  std::vector<Result> TakeResults();

 private:
  // One load, shared with its workers so it outlives a Cancel().
  struct Run {
    std::vector<Job> jobs;
    std::vector<Result> results;  // one slot per job; guarded by mutex
    std::shared_ptr<std::atomic<float>[]> progress;  // per job, 0-1
    std::atomic<size_t> next{0};
    std::atomic<size_t> done{0};
    std::atomic<bool> cancel{false};

    mutable std::mutex mutex;
    std::string current;
  };

  static void Work(std::shared_ptr<Run> run);

  std::shared_ptr<Run> run_;
  std::vector<std::thread> workers_;
  bool active_ = false;
};

}  // namespace Mercury

#endif  // MERCURY_SCENE_MODEL_PRELOADER_H
