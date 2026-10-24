/*
 * parallel_for.h
 *
 * Runs independent pieces of work on every core, for loading code that has a
 * lot to do inside one model (decoding hundreds of textures, converting
 * thousands of meshes). The calling thread works too, and the call returns
 * when everything is done.
 */
#ifndef MERCURY_ALGORITHMS_PARALLEL_FOR_H
#define MERCURY_ALGORITHMS_PARALLEL_FOR_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace Mercury {

// Cores worth using; 1 where there are no threads (Emscripten).
inline size_t WorkerThreadCount() {
#if defined(__EMSCRIPTEN__)
  return 1;
#else
  return std::max<size_t>(1, std::thread::hardware_concurrency());
#endif
}

/**
 * @brief Calls fn(i) for every i in [0, count), spread over several threads
 *
 * Indices are handed out one at a time, so uneven work balances itself. The
 * first exception thrown by `fn` is rethrown here once every thread stops.
 * `fn` must not touch shared state without its own locking.
 */
template <typename Fn>
void ParallelFor(size_t count, Fn&& fn, size_t max_threads = 0) {
  const size_t limit = max_threads ? max_threads : WorkerThreadCount();
  const size_t threads = std::min(count, limit);
  if (threads <= 1) {
    for (size_t i = 0; i < count; i++) fn(i);
    return;
  }

  std::atomic<size_t> next{0};
  std::exception_ptr failure;
  std::mutex failure_mutex;
  auto work = [&] {
    for (size_t i = next++; i < count; i = next++) {
      try {
        fn(i);
      } catch (...) {
        std::lock_guard lock(failure_mutex);
        if (!failure) failure = std::current_exception();
        next = count;  // stop handing out work
        return;
      }
    }
  };

  std::vector<std::thread> helpers;
  helpers.reserve(threads - 1);
  for (size_t i = 1; i < threads; i++) helpers.emplace_back(work);
  work();
  for (auto& helper : helpers) helper.join();
  if (failure) std::rethrow_exception(failure);
}

}  // namespace Mercury

#endif  // MERCURY_ALGORITHMS_PARALLEL_FOR_H
