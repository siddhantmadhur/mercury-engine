// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "model_preloader.h"

#include <algorithm>

#include "debugger/debugger.h"

namespace Mercury {

void ModelPreloader::Start(std::vector<Job> jobs) {
  Cancel();
  auto run = std::make_shared<Run>();
  run->jobs = std::move(jobs);
  run->results.assign(run->jobs.size(), Result{});
  for (size_t i = 0; i < run->jobs.size(); i++)
    run->results[i].res_path = run->jobs[i].res_path;
  run->progress = std::make_shared<std::atomic<float>[]>(run->jobs.size());
  for (size_t i = 0; i < run->jobs.size(); i++) run->progress[i] = 0.0f;
  run_ = run;
  active_ = true;
  if (run->jobs.empty()) return;

  const size_t cores = std::max(1u, std::thread::hardware_concurrency());
  const size_t count = std::clamp<size_t>(cores > 1 ? cores - 1 : 1, 1, 4);
  for (size_t i = 0; i < std::min(count, run->jobs.size()); i++) {
    workers_.emplace_back([run] { Work(run); });
  }
}

void ModelPreloader::Work(std::shared_ptr<Run> run) {
  while (!run->cancel) {
    const size_t index = run->next++;
    if (index >= run->jobs.size()) return;
    const Job& job = run->jobs[index];
    {
      std::lock_guard lock(run->mutex);
      run->current = job.res_path;
    }
    std::string error;
    std::shared_ptr<const Model> model;
    try {
      model = Model::Load(job.file, job.settings, &error, &run->progress[index],
                          &run->cancel);
    } catch (const std::exception& e) {
      error = e.what();
    }
    if (!model && !run->cancel)
      PDebug::warn("Could not load model {}: {}", job.res_path, error);
    {
      std::lock_guard lock(run->mutex);
      run->results[index].model = std::move(model);
    }
    run->progress[index] = 1.0f;
    run->done++;
  }
}

float ModelPreloader::Fraction() const {
  if (!run_ || run_->jobs.empty()) return 0.0f;
  float sum = 0.0f;
  for (size_t i = 0; i < run_->jobs.size(); i++) sum += run_->progress[i];
  return sum / static_cast<float>(run_->jobs.size());
}

void ModelPreloader::Cancel() {
  if (run_) run_->cancel = true;
  // Wait for nothing: an import that is already running cannot be interrupted
  // inside Assimp, and the workers only need the state they hold. They exit
  // once the model is built, and their results are dropped with `run_`.
  for (auto& worker : workers_) worker.detach();
  workers_.clear();
  run_.reset();
  active_ = false;
}

std::string ModelPreloader::CurrentName() const {
  if (!run_) return {};
  std::lock_guard lock(run_->mutex);
  return run_->current;
}

std::vector<ModelPreloader::Result> ModelPreloader::TakeResults() {
  auto run = std::move(run_);
  run_.reset();
  if (!run) return {};
  for (auto& worker : workers_) worker.join();
  workers_.clear();
  std::vector<Result> results;
  {
    std::lock_guard lock(run->mutex);
    results = std::move(run->results);
    run->results.clear();
  }
  active_ = false;
  return results;
}

}  // namespace Mercury
