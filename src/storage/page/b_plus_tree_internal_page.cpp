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

/**
 *   @brief 判断给定index是否在该page内
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::IsIndexValid(int index) const -> bool { return index >= 0 && index < GetSize(); }

/**
 *   @brief 在index处插入键值对，将后续键值对后移
 */
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

/**
 *   @brief 在index处改变value
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) { page_id_array_[index] = value; }

/**
 *   @brief 删除index处键值对，将后续键值对前移
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemovePairAt(int index) {
  int size = GetSize();
  for (int i = index; i < size - 1; i++) {  // 不用更新最后一个值
    key_array_[i] = key_array_[i + 1];
    page_id_array_[i] = page_id_array_[i + 1];
  }
  ChangeSizeBy(-1);  // 删除后size--
}

/**
 *   @brief  将本page键值对的[split_index + 1, size)移动到another_page键值对的[1, size-split_index)
 *           本page键值对的第split_index项的value设置为another_page的第0项值
 *           删除本page的[split_index, size)的键值对
 *
 *   @param another_page 要被分裂的page，必须为空
 *   @param split_idnex 指定被分裂的index,可以是min_size或min_size-1
 *   @param split_value 若有值则说明在another_page保留split_index处的键值对，同时another的第0处键为该值
 *
 *   @return 返回本page第split_index项的key
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::SplitHalfPairTo(BPlusTreeInternalPage *another_page, int split_index,
                                                     std::optional<ValueType> split_value) -> KeyType {
  int size = GetSize();
  int move_size = size - split_index;  // 记录移动的键值对数
  KeyType res = KeyAt(split_index);    // 记录返回的key,在split_value有值时该返回值无意义
  if (split_value.has_value()) {
    // 若split_value有值，则another第0项键设为split_value
    another_page->page_id_array_[0] = split_value.value();
    // 进行移动,本页的[split_index, size)移到another[1, move_size]
    for (int i = 1; i <= move_size; i++) {
      another_page->key_array_[i] = key_array_[split_index + i - 1];
      another_page->page_id_array_[i] = page_id_array_[split_index + i - 1];
    }
    another_page->ChangeSizeBy(move_size);  // another增加move_size
  } else {
    // 否则，将another第0项键设为本页split_index处的键
    another_page->page_id_array_[0] = page_id_array_[split_index];
    // 进行移动,将本页(split_index, size)的键值对移动到another的[1, move_size)上
    for (int i = 1; i < move_size; i++) {
      another_page->key_array_[i] = key_array_[split_index + i];
      another_page->page_id_array_[i] = page_id_array_[split_index + i];
    }
    another_page->ChangeSizeBy(move_size - 1);  // another增加move_size-1;减的这个1即为传到父页的this.keyAt(split_index)
  }
  ChangeSizeBy(-move_size);  // this减少了move_size的键值对
  return res;
}

/**
 * @brief 将another_page的键值对插入到本page(合并case)
 *        若another_page.size+this_page.size小于max_size,则将其所有键值对都插入本键值对;
 *        否则只插入到使两个page的size相同
 *        插入到本页的第一项的key为insert_key
 *
 * @param  insert_key 表示插入本页的key
 * @param is_another_larger 表示另一页是否在本页的右边，即是否比本页更大
 * @return 若another全部插入则返回true;否则返回false
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MergePairFrom(BPlusTreeInternalPage *another_page, bool is_another_larger,
                                                   const KeyType &insert_key) {
  int this_size = GetSize();
  int another_size = another_page->GetSize();

  // 获取需要移动的键值对数
  int move_size;                                   // 记录another_page需要移动的键值对数
  if (this_size + another_size <= GetMaxSize()) {  // 将another的所有键值对插入本page
    move_size = another_size;
  } else {  // 平均两个page的键值对
    move_size = another_size - (std::ceil((this_size + another_size) / 2.0));
  }

  // 进行移动
  if (is_another_larger) {  // 若another_page更大，则将another的前move_size个键值对移到this的后面
    // 在this的后面新添键值对
    for (int i = 0; i < move_size; i++) {
      if (i != 0) {  // 避免访问key_array[0]
        key_array_[this_size + i] = another_page->key_array_[i];
      } else {  // i==0时，需要新添本页的键
        key_array_[this_size] = insert_key;
      }
      page_id_array_[this_size + i] = another_page->page_id_array_[i];
    }
    // 删除another的前move_size个键值对;another_page的0处key仍赋值，方便改变父页
    for (int i = 0; i < another_size - move_size; i++) {
      another_page->key_array_[i] = another_page->key_array_[move_size + i];
      another_page->page_id_array_[i] = another_page->page_id_array_[move_size + i];
    }
  } else {  // 若another_page更小，则将another的后move_size个键值对移到this的前面
    // 将this整体后移move_size个键值对，为新添键值对留空
    for (int i = this_size - 1; i >= 0; i--) {
      if (i != 0) {  // 避免访问key_array[0]
        key_array_[move_size + i] = key_array_[i];
      } else {  // i==0时，需要新添本页的键
        key_array_[move_size] = insert_key;
      }
      page_id_array_[move_size + i] = page_id_array_[i];
    }
    // 将another的后move_size键值对移动到this前;this的0处key仍赋值，方便改变父页
    for (int i = 0; i < move_size; i++) {
      key_array_[i] = another_page->key_array_[another_size - move_size + i];
      page_id_array_[i] = another_page->page_id_array_[another_size - move_size + i];
    }
  }

  ChangeSizeBy(move_size);                 // this增加了move_size
  another_page->ChangeSizeBy(-move_size);  // another减少了move_size
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
