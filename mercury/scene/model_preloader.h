/*
 * model_preloader.h
 *
 * Loads model files on background threads so a big map does not freeze the
 * editor or the game. Model::Load only produces CPU data (the renderer
 * uploads it to the GPU later, on the main thread), so a worker can run it;
 * the caller polls for progress and takes the finished models.
 *
 * Only the thread that calls Start() may use the other members.
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
    std::shared_ptr<const Model> model;  // null if the model could not be loaded
  };

  ModelPreloader() = default;
  ~ModelPreloader() { Cancel(); }
  ModelPreloader(const ModelPreloader&) = delete;
  ModelPreloader& operator=(const ModelPreloader&) = delete;

  // Starts loading; a load already in progress is cancelled first. Without
  // threads (Emscripten) the models are loaded before this returns.
  void Start(std::vector<Job> jobs);

  // Stops after the models being loaded now and drops every result.
  void Cancel();

  // True from Start until TakeResults or Cancel.
  bool IsBusy() const { return active_; }
  // True once every job has run.
  bool IsFinished() const { return active_ && done_ >= jobs_.size(); }

  size_t Done() const { return done_; }
  size_t Total() const { return jobs_.size(); }
  // How much of all the work is done (0-1), counting the models still loading
  // by how far along they are.
  float Fraction() const;
  // The model a worker started most recently, for a progress label.
  std::string CurrentName() const;

  // The results of a finished load, in job order. Ends the load.
  std::vector<Result> TakeResults();

 private:
  void Work();

  std::vector<Job> jobs_;
  std::vector<std::thread> workers_;
  std::atomic<size_t> next_{0};
  std::atomic<size_t> done_{0};
  std::unique_ptr<std::atomic<float>[]> progress_;  // per job, 0-1
  std::atomic<bool> cancel_{false};
  bool active_ = false;

  mutable std::mutex mutex_;
  std::vector<Result> results_;  // one slot per job; guarded by mutex_
  std::string current_;
};

}  // namespace Mercury

#endif  // MERCURY_SCENE_MODEL_PRELOADER_H
