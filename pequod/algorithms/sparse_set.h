//
// Created by smadhur on 5/12/2026.
//

#ifndef PEQUOD_ENGINE_SPARSE_SET_H
#define PEQUOD_ENGINE_SPARSE_SET_H
#include <vector>

namespace Pequod {

/**
 * @brief Sparse set implementation
 */
template <class T>
class SparseSet {
  using kIndex = unsigned int;

 public:
  SparseSet() { dense_array_ = new T[dense_array_capacity_]; };

  T* Get(kIndex index) {
    if (index < dense_array_capacity_) {
      return &dense_array_[index];
    }
    return nullptr;
  };
  void Set(kIndex idx, const T& val) {
    if (idx > dense_array_capacity_) {
      auto* temp_ptr = new T[dense_array_capacity_ * 2];
      std::copy(dense_array_,
                dense_array_ + (sizeof(T) * dense_array_capacity_), temp_ptr);
      delete dense_array_;
      dense_array_capacity_ *= 2;
      dense_array_ = temp_ptr;
    }
    dense_array_[idx] = T(val);
    sparse_array_.push_back(idx);
  };

  void Delete(kIndex idx) {
    if (idx < sparse_array_.size()) {
    }
  };

 private:
  std::vector<kIndex> sparse_array_;
  T* dense_array_ = nullptr;
  unsigned int dense_array_capacity_ = 16;
  int max_size_ = 0;
};
}  // namespace Pequod

#endif  // PEQUOD_ENGINE_SPARSE_SET_H
