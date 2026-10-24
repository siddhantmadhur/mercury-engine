#include "model_preloader.h"

#include <algorithm>

#include "debugger/debugger.h"

namespace Mercury {

void ModelPreloader::Start(std::vector<Job> jobs) {
  Cancel();
  jobs_ = std::move(jobs);
  results_.assign(jobs_.size(), Result{});
  for (size_t i = 0; i < jobs_.size(); i++) results_[i].res_path = jobs_[i].res_path;
  progress_.reset(new std::atomic<float>[jobs_.size()]());
  next_ = 0;
  done_ = 0;
  cancel_ = false;
  active_ = true;
  if (jobs_.empty()) return;

#if defined(__EMSCRIPTEN__)
  Work();
#else
  const size_t cores = std::max(1u, std::thread::hardware_concurrency());
  const size_t count = std::clamp<size_t>(cores > 1 ? cores - 1 : 1, 1, 4);
  for (size_t i = 0; i < std::min(count, jobs_.size()); i++) {
    workers_.emplace_back([this] { Work(); });
  }
#endif
}

void ModelPreloader::Work() {
  while (!cancel_) {
    const size_t index = next_++;
    if (index >= jobs_.size()) return;
    const Job& job = jobs_[index];
    {
      std::lock_guard lock(mutex_);
      current_ = job.res_path;
    }
    std::string error;
    std::shared_ptr<const Model> model;
    try {
      model = Model::Load(job.file, job.settings, &error, &progress_[index]);
    } catch (const std::exception& e) {
      error = e.what();
    }
    if (!model) PDebug::warn("Could not load model {}: {}", job.res_path, error);
    {
      std::lock_guard lock(mutex_);
      results_[index].model = std::move(model);
    }
    progress_[index] = 1.0f;
    done_++;
  }
}

float ModelPreloader::Fraction() const {
  if (jobs_.empty() || !progress_) return 0.0f;
  float sum = 0.0f;
  for (size_t i = 0; i < jobs_.size(); i++) sum += progress_[i];
  return sum / static_cast<float>(jobs_.size());
}

void ModelPreloader::Cancel() {
  cancel_ = true;
  for (auto& worker : workers_) worker.join();
  workers_.clear();
  jobs_.clear();
  results_.clear();
  current_.clear();
  progress_.reset();
  done_ = 0;
  active_ = false;
}

std::string ModelPreloader::CurrentName() const {
  std::lock_guard lock(mutex_);
  return current_;
}

std::vector<ModelPreloader::Result> ModelPreloader::TakeResults() {
  for (auto& worker : workers_) worker.join();
  workers_.clear();
  std::vector<Result> results;
  {
    std::lock_guard lock(mutex_);
    results = std::move(results_);
    results_.clear();
  }
  jobs_.clear();
  current_.clear();
  progress_.reset();
  done_ = 0;
  active_ = false;
  return results;
}

}  // namespace Mercury
