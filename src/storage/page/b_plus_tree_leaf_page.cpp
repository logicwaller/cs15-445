//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2018-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * Init method after creating a new leaf page
 * Including set page type, set current size to zero, set next page id and set max size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);  // 设置page_size_
  SetSize(0);                             // 设置size_=0
  next_page_id_ = INVALID_PAGE_ID;        // TODO:设置next_page_id_
  SetMaxSize(max_size);                   // 设置max_size_
}

/**
 * Helper methods to set/get next page id
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t { return next_page_id_; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

/*
 * Helper method to find and return the key associated with input "index" (a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType { return key_array_[index]; }

/*
 * 返回index处的value
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType { return rid_array_[index]; }

/*
 * 在index处插入键值对(key,value),将后续的键值对后移
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::InsertPairAt(int index, const KeyType &key, const ValueType &value) {
  // 将index后的键值对后移
  int size = GetSize();
  for (int i = size; i > index; i--) {
    key_array_[i] = key_array_[i - 1];
    rid_array_[i] = rid_array_[i - 1];
  }
  // 在index处插入键值对
  key_array_[index] = key;
  rid_array_[index] = value;
  ChangeSizeBy(1);  // 插入后size++
}

/*
 * 在index处删除键值对,将后续的键值对前移
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::RemovePairAt(int index) {
  int size = GetSize();
  for (int i = index; i < size - 1; i++) {  // 不用更新最后一个值
    key_array_[i] = key_array_[i + 1];
    rid_array_[i] = rid_array_[i + 1];
  }
  ChangeSizeBy(-1);  // 删除后size--
}

/*
 * 将本page键值对的[minsize, size)移动到another_page键值对的[0, size-half_size-1]
 * 注：another_page必须没有键值对
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveHalfPairTo(BPlusTreeLeafPage *another_page) {
  int half_size = GetMinSize();
  int size = GetSize();
  for (int i = 0; i < size - half_size; i++) {
    another_page->InsertPairAt(i, KeyAt(half_size), ValueAt(half_size));
  }
  ChangeSizeBy(-(size - half_size));  // this减少了size - half_size的键值对
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
