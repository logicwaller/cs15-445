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
  std::deque<int> find_ids = FindLeafPage(key, ctx);
  ReadPageGuard leaf_page_guard = bpm_->ReadPage(*find_ids.end());
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
    // 将root_page_id写入head_page
    WritePageGuard head_page_guard = bpm_->WritePage(header_page_id_);
    auto head_page = head_page_guard.AsMut<BPlusTreeHeaderPage>();
    head_page->root_page_id_ = new_page_id;
    // 在0处插入键值对
    root_page->InsertPairAt(0, key, value);
    return true;
  }

  // 获取key应在的leafpage
  std::deque<int> find_ids = FindLeafPage(key, ctx);
  WritePageGuard leaf_page_guard = bpm_->WritePage(*find_ids.end());
  // 向leafpage插入键值对
  auto leaf_page = leaf_page_guard.AsMut<LeafPage>();
  int insert_id = KeyBinarySearch(leaf_page, key);           // 获取应插入的index
  if (comparator_(leaf_page->KeyAt(insert_id), key) == 0) {  // 若存在相同key,则返回false
    return false;
  }
  leaf_page->InsertPairAt(insert_id, key, value);

  // 向leaf_page插入后若达到maxsize则进行分裂
  if (leaf_page->GetSize() == leaf_page->GetMaxSize()) {
    SplitLeafPage(find_ids);
  }
  return true;
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
  ctx.root_page_id_ = GetRootPageId();
  if (ctx.root_page_id_ == INVALID_PAGE_ID) {  // 若数为空，直接返回
    return;
  }

  // 查找key应在的leafpage
  std::deque<int> find_ids = FindLeafPage(key, ctx);
  WritePageGuard leaf_page_guard = bpm_->WritePage(*find_ids.end());
  auto leaf_page = leaf_page_guard.AsMut<LeafPage>();
  // 查找key是否存在于该leafpage
  int find_index = KeyBinarySearch(leaf_page, key);
  if (comparator_(leaf_page->KeyAt(find_index), key) != 0) {  // 若该key不存在于page,直接返回
    return;
  }

  // 进行删除
  leaf_page->RemovePairAt(find_index);

  // 向leaf_page删除后若小于minsize则进行合并
  if (leaf_page->GetSize() < leaf_page->GetMinSize()) {
    // TODO:进行合并
    MergeLeafPage(find_ids);
  }
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

/*****************************************************************************
 * 辅助函数
 *****************************************************************************/

/**
 * @brief 在page内的key_array进行二分查找key
 *        在internalpage内查找index,使key_array[index] <= key < key_array[index + 1];若key<key_array[1]则返回0】
 *        在leafpage内查找index,若完全匹配则返回匹配值,否则返回index使key_array[index - 1] < key < key_array[index]
 *        即两种page的所需算法大体一致，但不完全匹配时，leaf所需index=internal所需index + 1
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename PageType>
auto BPLUSTREE_TYPE::KeyBinarySearch(const PageType *page, const KeyType &key) const -> int {
  int left = 0;
  int right = page->GetSize() - 1;
  // 进行二分查找
  while (left <= right) {
    int mid_index = std::floor((left + right) / 2);

    // internalpage的key_array[0]不存在，当查找到这一项时说明key < key_array[1],直接返回0即可
    if (!page->IsLeafPage() && mid_index == 0) {
      return 0;
    }

    KeyType mid = page->KeyAt(mid_index);
    if (comparator_(mid, key) > 0) {  // mid > key
      right = mid_index - 1;
    } else if (comparator_(mid, key) < 0) {  // mid < key
      left = mid_index + 1;
    } else {             // mid == key
      return mid_index;  // 完全匹配时，直接返回该值
    }
  }
  // 不完全匹配时，进行处理;此时left = right + 1,且key_array[right] < key < key_array[left]
  if (page->IsLeafPage()) {  // 若为leafpage
    return left;
  } else {  // 若为internalpage
    return right;
  }
}

/**
 * @brief 查找key应当存在的leafpage
 *        使用乐观读取,即读取该页后释放父页latch
 *
 * @return 返回查找路径上所有的page_id，返回队列的最后一项为找到的leafpage
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeafPage(const KeyType &key, Context &ctx) const -> std::deque<int> {
  std::deque<int> res_vector;
  page_id_t now_page_id = ctx.root_page_id_;
  while (true) {
    res_vector.push_back(now_page_id);  // 添加now_page_id

    ReadPageGuard guard = bpm_->ReadPage(now_page_id);

    // TODO(logicwaller):怎么加锁释放锁
    // if (!ctx.read_set_.empty()) {  // 确定该页能被读取后再释放父页
    //   ctx.read_set_.pop_back();
    // }
    // ctx.read_set_.push_back(guard);  // 加锁该页

    auto judge = guard.As<BPlusTreePage>();  // 先转换为父类，判断是internalpage或leafpage
    if (judge->IsLeafPage()) {               // 若是leafpage,返回
      return res_vector;
    }
    // 若是internalpage，则继续查找
    auto now_page = guard.As<InternalPage>();
    int find_index = KeyBinarySearch(now_page, key);
    now_page_id = now_page->ValueAt(find_index);
  }
}

/**
 * @brief 分裂leafpage,即ancestor_page_id的最后一项,向父页插入新节点
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::SplitLeafPage(std::deque<int> &ancestor_page_id) {
  // 读取要被分裂的leafpage
  page_id_t old_leaf_page_id = *ancestor_page_id.end();
  WritePageGuard old_leaf_page_guard = bpm_->WritePage(old_leaf_page_id);
  auto old_leaf_page = old_leaf_page_guard.AsMut<LeafPage>();
  // 初始化新页
  page_id_t new_leaf_page_id = bpm_->NewPage();
  WritePageGuard new_leaf_page_guard = bpm_->WritePage(new_leaf_page_id);
  auto new_leaf_page = new_leaf_page_guard.AsMut<LeafPage>();
  new_leaf_page->Init();
  // 将old_leaf_page一半以后的数据剪切到new_leaf_page
  old_leaf_page->MoveHalfPairTo(new_leaf_page);
  // 获取新产生的节点的key
  KeyType new_key = new_leaf_page->KeyAt(0);  // new_leaf_page的第一项即为新添key
  // 向leaf_page的父页进行插入
  if (!ancestor_page_id.empty()) {
    InsertPairToInternalPage(ancestor_page_id, new_key, new_leaf_page_id);
  } else {  // 若没有祖节点，则需要传入左page_id
    InsertPairToInternalPage(ancestor_page_id, new_key, new_leaf_page_id, old_leaf_page_id);
  }
}

/**
 * @brief (SplitLeafPage的辅助函数)向ancestor_page_id的最后一项插入键值对，若需要分裂则进行递归
 *        默认lvalue是空值
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertPairToInternalPage(std::deque<int> &ancestor_page_id, const KeyType &key,
                                              const page_id_t &value, const page_id_t &lvalue) {
  // 获取父页
  page_id_t parent_page_id;
  WritePageGuard parent_page_guard;
  if (ancestor_page_id.empty()) {  // 若无ancestor，则说明需要在根页添加新的一层
    //  初始化新的root_page
    page_id_t new_root_page_id = bpm_->NewPage();
    parent_page_guard = bpm_->WritePage(new_root_page_id);
    auto new_root_page = parent_page_guard.AsMut<InternalPage>();
    new_root_page->Init();
    // 更新root_page_id
    WritePageGuard head_page_guard = bpm_->WritePage(header_page_id_);
    auto head_page = head_page_guard.AsMut<BPlusTreeHeaderPage>();
    head_page->root_page_id_ = new_root_page_id;
    // 向新的root_page插入键值对:0处的值为lvalue,1处的键值对为(key, value)
    new_root_page->InsertPairAt(1, key, value);
    new_root_page->SetValueAt(0, lvalue);  // 在0处设置lvalue
  } else {                                 // 否则获取ancestor最后一项
    parent_page_id = *ancestor_page_id.end();
    parent_page_guard = bpm_->WritePage(parent_page_id);
    ancestor_page_id.pop_back();  // 将最后一项去除
  }

  // 判断父页是否需要分裂
  auto parent_page = parent_page_guard.AsMut<InternalPage>();
  if (parent_page->GetSize() == parent_page->GetMaxSize()) {  // 达到maxsize，需要递归分裂
    // 创建新页
    page_id_t new_page_id = bpm_->NewPage();
    WritePageGuard new_page_guard = bpm_->WritePage(new_page_id);
    auto new_page = new_page_guard.AsMut<InternalPage>();
    new_page->Init();
    // 将旧页一半以后的内容粘贴到新页
    KeyType new_key = parent_page->MoveHalfPairTo(new_page);  // 返回值为插入父页的key
    // 检查下次递归时是否需要传递lvalue
    if (ancestor_page_id.empty()) {  // 若为空，则需要传递parent_pair_id作为lvalue
      InsertPairToInternalPage(ancestor_page_id, new_key, new_page_id, parent_page_id);
    } else {
      InsertPairToInternalPage(ancestor_page_id, new_key, new_page_id);
    }
  }

  // 分裂完成后进行插入
  int insert_index = KeyBinarySearch(parent_page, key) + 1;  // 插入位置为internalpage查找位置+1
  parent_page->InsertPairAt(insert_index, key, value);
}

/**
 * @brief 合并leafpage
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::MergeLeafPage(std::deque<int> &ancestor_page_id) {}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
