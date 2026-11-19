// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "algorithms/sparse_set.h"

#include <gtest/gtest.h>

#include <string>

using namespace Mercury;

TEST(SparseSet, StoresAndReplacesValuesById) {
  SparseSet<int> set;
  EXPECT_EQ(set.Get(0), nullptr);
  ASSERT_TRUE(set.Set(16, 1));  // past any starting capacity
  ASSERT_TRUE(set.Set(3, 7));
  ASSERT_TRUE(set.Set(16, 2));
  EXPECT_EQ(set.Size(), 2u);
  EXPECT_EQ(*set.Get(16), 2);
  EXPECT_EQ(*set.Get(3), 7);
  EXPECT_EQ(set.Get(4), nullptr);
  EXPECT_FALSE(set.Contains(1000));
}

TEST(SparseSet, DeleteKeepsTheOthersReachable) {
  SparseSet<std::string> set;
  for (unsigned i = 0; i < 100; i++) set.Set(i * 3, std::to_string(i));
  set.Delete(0);
  set.Delete(150);
  set.Delete(150);  // already gone
  set.Delete(999999);  // never there
  EXPECT_EQ(set.Size(), 98u);
  EXPECT_EQ(set.Get(0), nullptr);
  EXPECT_EQ(set.Get(150), nullptr);
  for (unsigned i = 0; i < 100; i++) {
    if (i == 0 || i == 50) continue;
    ASSERT_NE(set.Get(i * 3), nullptr) << i;
    EXPECT_EQ(*set.Get(i * 3), std::to_string(i));
  }
  set.Set(0, "again");
  EXPECT_EQ(*set.Get(0), "again");
}

TEST(SparseSet, RefusesIdsThatWouldAllocateHugeTables) {
  SparseSet<int> set;
  EXPECT_FALSE(set.Set(SparseSet<int>::kMaxIndex + 1, 1));
  EXPECT_FALSE(set.Set(0xFFFFFFFFu, 1));
  EXPECT_TRUE(set.Empty());
  set.Set(5, 1);
  set.Clear();
  EXPECT_EQ(set.Get(5), nullptr);
}
