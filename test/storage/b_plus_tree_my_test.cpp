//===----------------------------------------------------------------------===//
//
//                         BusTub
//      额外测试文件
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdio>

#include "buffer/buffer_pool_manager.h"
#include "gtest/gtest.h"
#include "storage/disk/disk_manager_memory.h"
#include "storage/index/b_plus_tree.h"
#include "test_util.h"  // NOLINT

#include <random>

namespace bustub {

using bustub::DiskManagerUnlimitedMemory;

TEST(BPlusTreeTests, BasicScaleTest) {  // 测试大规模插入删除
  // create KeyComparator and index schema
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(30, disk_manager.get());

  // allocate header_page
  page_id_t page_id = bpm->NewPage();

  // create b+ tree
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", page_id, bpm, comparator, 2, 3);
  GenericKey<8> index_key;
  RID rid;

  int64_t scale = 5000;
  std::vector<int64_t> keys(scale);
  std::iota(keys.begin(), keys.end(), 1);

  // randomized the insertion order
  auto rng = std::default_random_engine{};
  std::shuffle(keys.begin(), keys.end(), rng);
  std::deque<int64_t> remove_key;
  std::vector<int64_t> exit_key;
  [[maybe_unused]] int i = 0;  // 添加计数，方便debug在什么时候出错
  for (auto key : keys) {
    i++;
    if (i % 2 == 0) {
      remove_key.push_back(key);
    } else {
      exit_key.push_back(key);
    }
    int64_t value = key & 0xFFFFFFFF;
    rid.Set(static_cast<int32_t>(key >> 32), value);
    index_key.SetFromInteger(key);
    tree.Insert(index_key, rid);
  }
  std::vector<RID> rids;
  for (auto key : keys) {
    rids.clear();
    index_key.SetFromInteger(key);
    tree.GetValue(index_key, &rids);
    ASSERT_EQ(rids.size(), 1);

    int64_t value = key & 0xFFFFFFFF;
    ASSERT_EQ(rids[0].GetSlotNum(), value);
  }

  for (auto key : remove_key) {
    int64_t value = key & 0xFFFFFFFF;
    rid.Set(static_cast<int32_t>(key >> 32), value);
    index_key.SetFromInteger(key);
    tree.Remove(index_key);
  }

  int64_t size = 0;
  std::sort(exit_key.begin(), exit_key.end());
  for (auto it = tree.Begin(); it != tree.End(); ++it) {
    auto key = exit_key[size];
    rids.clear();
    index_key.SetFromInteger(key);
    tree.GetValue(index_key, &rids);
    ASSERT_EQ(rids.size(), 1);

    int64_t value = key & 0xFFFFFFFF;
    ASSERT_EQ(rids[0].GetSlotNum(), value);
    size++;
  }
  ASSERT_EQ(exit_key.size(), size);
  delete bpm;
}

TEST(BPlusTreeTests, DISABLED_Insert2Test) {  // 测试顺序插入时是否维护正确的锁(没有写测试，一步步degug看罢)
  // create KeyComparator and index schema
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  // allocate header_page
  page_id_t page_id = bpm->NewPage();
  // create b+ tree
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", page_id, bpm, comparator, 3, 5);
  GenericKey<8> index_key;
  RID rid;

  std::vector<int64_t> keys = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
  for (auto key : keys) {
    int64_t value = key & 0xFFFFFFFF;
    rid.Set(static_cast<int32_t>(key >> 32), value);
    index_key.SetFromInteger(key);
    tree.Insert(index_key, rid);
    std::cout << "inserting: " << key << std::endl;
    std::cout << tree.DrawBPlusTree() << std::endl;
    tree.Print(bpm);
    std::cout << "=====================================================" << std::endl;
  }

  bool is_present;
  std::vector<RID> rids;

  for (auto key : keys) {
    rids.clear();
    index_key.SetFromInteger(key);
    is_present = tree.GetValue(index_key, &rids);

    EXPECT_EQ(is_present, true);
    EXPECT_EQ(rids.size(), 1);
    int64_t value = key & 0xFFFFFFFF;
    EXPECT_EQ(rids[0].GetSlotNum(), value);
  }
  delete bpm;
}
}  // namespace bustub
