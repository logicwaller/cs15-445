//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2018-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/
/*
 * Init method after creating a new internal page
 * Including set page type, set current size, and set max page size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);  // 设置page_type_
  SetSize(1);                                 // 设置size_=1(key_array的第一个值为空)
  SetMaxSize(max_size);                       // 设置max_size_
}

/*
 * Helper method to get/set the key associated with input "index" (a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType { return key_array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) { key_array_[index] = key; }

/*
 * Helper method to get the value associated with input "index" (a.k.a array
 * offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType { return page_id_array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertPairAt(int index, const KeyType &key, const ValueType &value) {
  // 将index后的键值对后移
  int size = GetSize();
  for (int i = size; i > index; i--) {
    key_array_[i] = key_array_[i - 1];
    page_id_array_[i] = page_id_array_[i - 1];
  }
  // 在index处插入键值对
  key_array_[index] = key;
  page_id_array_[index] = value;
  ChangeSizeBy(1);  // 插入后size++
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) { page_id_array_[index] = value; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemovePairAt(int index) {
  int size = GetSize();
  for (int i = index; i < size - 1; i++) {  // 不用更新最后一个值
    key_array_[i] = key_array_[i + 1];
    page_id_array_[i] = page_id_array_[i + 1];
  }
  ChangeSizeBy(-1);  // 删除后size--
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::SplitHalfPairTo(BPlusTreeInternalPage *another_page) -> KeyType {
  int min_size = GetMinSize();
  int size = GetSize();
  int another_page_size = another_page->GetSize();  // 分裂情况下该值一定是0
  int move_size = size - min_size;                  // 记录移动的键值对数
  KeyType res = KeyAt(min_size);                    // 记录返回的key
  // 进行移动
  another_page->page_id_array_[0] = page_id_array_[min_size];  // 将本页min_size项的值移动到another的第0项
  for (int i = 1; i < move_size; i++) {  // 移动[min_size+1, size)的键值对到another_page的[1, size-min_size)
    another_page->key_array_[another_page_size + i] = key_array_[min_size + i];
    another_page->page_id_array_[another_page_size + i] = page_id_array_[min_size + i];
  }
  another_page->ChangeSizeBy(move_size - 1);  // another增加move_size-1;减的这个1即为传到父页的this.keyAt(minsize)
  ChangeSizeBy(-move_size);                   // this减少了move_size的键值对
  return res;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::MergePairFrom(BPlusTreeInternalPage *another_page, bool is_another_larger)
    -> bool {
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
      page_id_array_[this_size + i] = another_page->page_id_array_[i];
    }
    // 删除another的前move_size个键值对
    for (int i = 0; i < another_size - move_size; i++) {
      another_page->key_array_[i] = another_page->key_array_[move_size + i];
      another_page->page_id_array_[i] = another_page->page_id_array_[move_size + i];
    }
  } else {  // 若another_page更小，则将another的后move_size个键值对移到this的前面
    for (int i = 0; i < move_size; i++) {
      // 将this的前move_size键值对整体后移，为新添键值对留空
      key_array_[move_size + i] = key_array_[i];
      page_id_array_[move_size + i] = page_id_array_[i];
      // 将another的后move_size键值对移动到this前
      key_array_[i] = another_page->key_array_[another_size - move_size + i];
      page_id_array_[i] = another_page->page_id_array_[another_size - move_size + i];
    }
  }
  ChangeSizeBy(move_size);                 // this增加了move_size
  another_page->ChangeSizeBy(-move_size);  // another减少了move_size
  return res;
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
