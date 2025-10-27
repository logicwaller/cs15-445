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
 * 将本page键值对minsize以后的键值对移动到another_page键值对队尾(分裂case)
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SplitHalfPairTo(BPlusTreeLeafPage *another_page) {
  int min_size = GetMinSize();
  int size = GetSize();
  int another_page_size = another_page->GetSize();  // 分裂情况下该值一定是0
  int move_size = size - min_size;                  // 记录移动的键值对数
  // 进行移动
  for (int i = 0; i < move_size; i++) {  // 移动[min_size, size)的键值对到another_page的队尾
    another_page->key_array_[another_page_size + i] = key_array_[min_size + i];
    another_page->rid_array_[another_page_size + i] = rid_array_[min_size + i];
  }
  another_page->ChangeSizeBy(move_size);  // another_page增加move_size
  ChangeSizeBy(-move_size);               // this减少了move_size的键值对
}

/**
 * @brief 将another_page的键值对插入到本page(合并case)
 *        若another_page.size+this_page.size小于max_size,则将其所有键值对都插入本键值对;否则只插入到使两个page的size相同
 * @param  is_another_larger 表示another_page内的键是否比this大
 * @return 若another全部插入则返回true;否则返回false
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::MergePairFrom(BPlusTreeLeafPage *another_page, bool is_another_larger) -> bool {
  int this_size = GetSize();
  int another_size = another_page->GetSize();

  // 获取需要移动的键值对数
  int move_size;  // 记录another_page需要移动的键值对数
  bool res;
  if (this_size + another_size < GetMaxSize()) {  // 将another的所有键值对插入本page
    move_size = another_size;
    res = true;
  } else {  // 平均两个page的键值对
    move_size = another_size - (std::ceil((this_size + another_size) / 2));
    res = false;
  }

  // 进行移动
  if (is_another_larger) {  // 若another_page更大，则将another的前move_size个键值对移到this的后面
    // 在this的后面新添键值对
    for (int i = 0; i < move_size; i++) {
      key_array_[this_size + i] = another_page->key_array_[i];
      rid_array_[this_size + i] = another_page->rid_array_[i];
    }
    // 删除another的前move_size个键值对
    for (int i = 0; i < another_size - move_size; i++) {
      another_page->key_array_[i] = another_page->key_array_[move_size + i];
      another_page->rid_array_[i] = another_page->rid_array_[move_size + i];
    }
  } else {  // 若another_page更小，则将another的后move_size个键值对移到this的前面
    for (int i = 0; i < move_size; i++) {
      // 将this的前move_size键值对整体后移，为新添键值对留空
      key_array_[move_size + i] = key_array_[i];
      rid_array_[move_size + i] = rid_array_[i];
      // 将another的后move_size键值对移动到this前
      key_array_[i] = another_page->key_array_[another_size - move_size + i];
      rid_array_[i] = another_page->rid_array_[another_size - move_size + i];
    }
  }
  ChangeSizeBy(move_size);                 // this增加了move_size
  another_page->ChangeSizeBy(-move_size);  // another减少了move_size
  return res;
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
