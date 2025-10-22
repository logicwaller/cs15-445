#include "storage/index/b_plus_tree.h"
#include "storage/index/b_plus_tree_debug.h"

namespace bustub {

INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : index_name_(std::move(name)),
      bpm_(buffer_pool_manager),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  // 获取root_page,判断root_page是否存东西
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto header_page = guard.As<BPlusTreeHeaderPage>();
  ReadPageGuard root_guard = bpm_->ReadPage(header_page->root_page_id_);
  auto root_page = root_guard.As<InternalPage>();
  return root_page->GetSize() == 0;  // 若root_page没有东西则为空
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  // Declaration of context instance.
  Context ctx;
  ctx.root_page_id_ = GetRootPageId();
  if (ctx.root_page_id_ == INVALID_PAGE_ID) {  // 若root_page_id不存在,则直接返回false
    return false;
  }
  // 获取该key应该存在的page_id
  page_id_t leaf_page_id = FindLeafPage(key, ctx);
  ReadPageGuard leaf_page_guard = bpm_->ReadPage(leaf_page_id);
  ctx.read_set_.pop_back();  // 获取leafpage后，释放最后一次读取的internalpage
  // 查找key是否在该leafpage中
  auto leaf_page = leaf_page_guard.As<LeafPage>();
  int find_index = KeyBinarySearch(leaf_page, key);
  if (comparator_(leaf_page->KeyAt(find_index), key) != 0) {  // 若key_array[find_inde] != key,直接return false
    return false;
  }
  result->push_back(leaf_page->ValueAt(find_index));
  return true;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  // Declaration of context instance.
  Context ctx;
  ctx.root_page_id_ = GetRootPageId();
  if (ctx.root_page_id_ == INVALID_PAGE_ID) {  // 若root_page不存在,新建root_page
    page_id_t new_page_id = bpm_->NewPage();
    // 向root_page写入键值对
    WritePageGuard root_page_guard = bpm_->WritePage(new_page_id);
    auto root_page = root_page_guard.AsMut<LeafPage>();  // root_page初始为leafpage
    root_page->Init();                                   // 初始化leafpage
    root_page->InsertPairAt(0, key, value);              // 在0处插入键值对
    root_page->ChangeSizeBy(1);                          // size++
    // 将root_page_id写入head_page
    WritePageGuard head_page_guard = bpm_->WritePage(header_page_id_);
    auto head_page = head_page_guard.AsMut<BPlusTreeHeaderPage>();
    head_page->root_page_id_ = new_page_id;
    return true;
  }

  // 获取key应在的leafpage
  page_id_t leaf_page_id = FindLeafPage(key, ctx);
  // 向leafpage插入键值对
  WritePageGuard leaf_page_guard = bpm_->WritePage(leaf_page_id);
  ctx.read_set_.pop_back();  // 获取leafpage后，释放最后一次读取的internalpage
  auto leaf_page = leaf_page_guard.AsMut<LeafPage>();
  int insert_id = KeyBinarySearch(leaf_page, key);  // 获取应插入的index
  leaf_page->InsertPairAt(insert_id, key, value);
  leaf_page->ChangeSizeBy(1);  // size++
  // 向leaf_page插入后若达到maxsize则进行分裂
  if (leaf_page->GetSize() == leaf_page->GetMaxSize()) {
    // TODO:分裂
  }
  return false;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  // Declaration of context instance.
  Context ctx;
  (void)ctx;
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

/**
 * @return Page id of the root of this tree
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto root_page = guard.As<BPlusTreeHeaderPage>();
  return root_page->root_page_id_;
}

/**
 * @brief 在page内的key_array进行二分查找key
 *        在internalpage内返回key所对应的page_id;
 *        在leafpage内查找key所在的index,若如无法完全匹配则返回应插入的index
 *        即两种page的所需算法大体一致，但不完全匹配时，leaf所需的返回值=internal所需的返回值+1
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename PageType>
auto BPLUSTREE_TYPE::KeyBinarySearch(const PageType *page, const KeyType &key) const -> int {
  int left = 0;
  int right = page->GetSize() - 1;

  // 当left与right重合(仅发生在leafpage只有一个元素时)
  if (left == right) {
    if (comparator_(page->KeyAt(left), key) < 0) {  // key_array[left] < key,则插入位置为left+1
      return ++left;
    }
    // 否则key_array[left] > key,插入位置为left
    return left;
  }

  while (true) {
    int mid_index = std::ceil((left + right) / 2);
    KeyType mid = page->KeyAt(mid_index);
    if (comparator_(mid, key) > 0) {  // mid > key
      right = mid_index;
    } else if (comparator_(mid, key) < 0) {  // mid < key
      left = mid_index;
    } else {  // mid == key
      return mid_index;
    }

    if (left + 1 == right) {                              // 已经缩小到两个key之间
      if (page->IsLeafPage()) {                           // 若为leafpage
        if (comparator_(page->KeyAt(right), key) == 0) {  // 若key_array[right] == key
          return right;
        }
        if (comparator_(page->KeyAt(left), key) == 0) {  // 若key_array[left] == key
          return left;
        }
        if (comparator_(page->KeyAt(right), key) < 0) {  // 若key_array[right] < key
          return ++right;
        }
        // 此时能说明key_array[left] < key < key_array[right]
        return ++left;
      } else {                                            // 若为internalpage
        if (comparator_(page->KeyAt(right), key) <= 0) {  // 若key_array[right] <= key
          return right;
        }
        // 此时能说明key_array[left] <= key < key_array[right]
        return left;
      }
    }
  }
}

/**
 * @brief 返回key应当存在的leafpage的page_id
 *        使用乐观读取,即读取该页后释放父页latch
 *        注意:最后的一次读取的internalpage不会释放，需要在该函数执行后手动释放
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeafPage(const KeyType &key, Context &ctx) const -> page_id_t {
  page_id_t now_page_id = ctx.root_page_id_;
  while (true) {
    ReadPageGuard guard = bpm_->ReadPage(now_page_id);
    if (!ctx.read_set_.empty()) {  // 确定该页能被读取后再释放父页
      ctx.read_set_.pop_back();
    }
    ctx.read_set_.push_back(guard);  // 添加该页

    auto judge = guard.As<BPlusTreePage>();  // 先转换为父类，判断是internalpage或leafpage
    if (judge->IsLeafPage()) {               // 若是leafpage,返回该page_id
      return now_page_id;
    }
    auto now_page = guard.As<InternalPage>();
    int find_index = KeyBinarySearch(now_page, key);
    now_page_id = now_page->ValueAt(find_index);  // KeyBinarySearch实现时保证internalpage返回值一定有值
  }
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
