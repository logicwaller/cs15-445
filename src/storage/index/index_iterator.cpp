/**
 * index_iterator.cpp
 */
#include <cassert>

#include "storage/index/index_iterator.h"

namespace bustub {

/*
 * NOTE: you can change the destructor/constructor method here
 * set your own input parameters
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(page_id_t begin_page_id, int begin_index, BufferPoolManager *bpm)
    : page_id_(begin_page_id), index_(begin_index), bpm_(bpm) {}

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() = default;  // NOLINT

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool {
  ReadPageGuard guard = bpm_->ReadPage(page_id_);
  auto now_page = guard.As<LeafPage>();
  return now_page->GetNextPageId() == INVALID_PAGE_ID && index_ == now_page->GetSize();
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> std::pair<const KeyType &, const ValueType &> {
  ReadPageGuard guard = bpm_->ReadPage(page_id_);
  auto now_page = guard.As<LeafPage>();
  pair_.first = now_page->KeyAt(index_);
  pair_.second = now_page->ValueAt(index_);
  return pair_;
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  ReadPageGuard guard = bpm_->ReadPage(page_id_);
  auto now_page = guard.As<LeafPage>();
  if (index_ < now_page->GetSize() - 1) {
    index_++;
  } else {
    if (now_page->GetNextPageId() == INVALID_PAGE_ID) {  // 若本页为最后一页，则只增加index_以判断是否达到end()
      index_++;
    } else {
      page_id_ = now_page->GetNextPageId();
      index_ = 0;
    }
  }
  return *this;
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
