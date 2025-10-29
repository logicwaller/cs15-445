//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/index/index_iterator.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include <utility>
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

INDEX_TEMPLATE_ARGUMENTS
class IndexIterator {
  // 添加LeafPage的宏，和b_plus_tree.h一致
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

 public:
  // you may define your own constructor based on your member variables
  IndexIterator(page_id_t begin_page_id, int begin_index, BufferPoolManager *bpm);
  ~IndexIterator();  // NOLINT

  auto IsEnd() -> bool;

  auto operator*() -> std::pair<const KeyType &, const ValueType &>;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &itr) const -> bool { return page_id_ == itr.page_id_ && index_ == itr.index_; }

  auto operator!=(const IndexIterator &itr) const -> bool { return page_id_ != itr.page_id_ || index_ != itr.index_; }

 private:
  // add your own private member variables here
  page_id_t page_id_;  // 记录当前iterator所在的page_id
  int index_;          // 记录当前iterator所在页的index
  BufferPoolManager *bpm_;
  std::pair<KeyType, ValueType> pair_;
};

}  // namespace bustub
