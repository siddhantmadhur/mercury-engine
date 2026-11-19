// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//
// Created by smadhur on 5/12/2026.
//

#ifndef MERCURY_ENGINE_SPARSE_SET_H
#define MERCURY_ENGINE_SPARSE_SET_H

#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace Mercury {

/**
 * @brief A map from small integer ids to values, stored densely
 *
 * Values live packed in one array, so iterating and clearing are cheap, while
 * Get, Set and Delete by id are O(1). Ids above kMaxIndex are refused so a bad
 * id cannot make the id table allocate gigabytes.
 *
 * Pointers from Get() are invalidated by Set() of a new id and by Delete()
 * (which moves another value into the freed slot).
 */
template <class T>
class SparseSet {
 public:
  using Index = unsigned int;
  static constexpr Index kMaxIndex = (1u << 24) - 1;

  bool Contains(Index index) const {
    return index < sparse_.size() && sparse_[index] != kEmpty;
  }

  T* Get(Index index) {
    return Contains(index) ? &dense_[sparse_[index]] : nullptr;
  }
  const T* Get(Index index) const {
    return Contains(index) ? &dense_[sparse_[index]] : nullptr;
  }

  // Stores `value` under `index`, replacing any earlier one. Returns false
  // (and stores nothing) if the id is above kMaxIndex.
  bool Set(Index index, const T& value) {
    T copy(value);  // `value` may point into this set
    return Set(index, std::move(copy));
  }
  bool Set(Index index, T&& value) {
    if (index > kMaxIndex) return false;
    if (Contains(index)) {
      dense_[sparse_[index]] = std::move(value);
      return true;
    }
    if (index >= sparse_.size()) sparse_.resize(index + 1, kEmpty);
    sparse_[index] = static_cast<Index>(dense_.size());
    dense_.push_back(std::move(value));
    owners_.push_back(index);
    return true;
  }

  // Removes the value under `index`, if any.
  void Delete(Index index) {
    if (!Contains(index)) return;
    // Move the last value into the hole so the values stay packed.
    const Index slot = sparse_[index];
    const Index last_owner = owners_.back();
    if (slot != dense_.size() - 1) {
      dense_[slot] = std::move(dense_.back());
      owners_[slot] = last_owner;
      sparse_[last_owner] = slot;
    }
    dense_.pop_back();
    owners_.pop_back();
    sparse_[index] = kEmpty;
  }

  void Clear() {
    sparse_.clear();
    dense_.clear();
    owners_.clear();
  }

  std::size_t Size() const { return dense_.size(); }
  bool Empty() const { return dense_.empty(); }

 private:
  static constexpr Index kEmpty = std::numeric_limits<Index>::max();

  std::vector<Index> sparse_;  // id -> position in dense_, or kEmpty
  std::vector<T> dense_;       // the values, packed
  std::vector<Index> owners_;  // position in dense_ -> id
};
}  // namespace Mercury

#endif  // MERCURY_ENGINE_SPARSE_SET_H
