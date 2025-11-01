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
  // 获取root_page
  Context ctx;
  InitContext(ctx, false);

  if (ctx.root_page_id_ == INVALID_PAGE_ID) {  // 若root_page_id不存在,则直接返回false
    return false;
  }
  // 获取该key应该存在的page_id
  ReadPageGuard leaf_page_guard;
  OptSearchLeafPage<ReadPageGuard>(key, ctx, leaf_page_guard);
  // 查找key是否在该leafpage中
  auto leaf_page = leaf_page_guard.As<LeafPage>();
  int find_index = KeyBinarySearch(leaf_page, key);
  if (comparator_(leaf_page->KeyAt(find_index), key) != 0) {  // 若key_array[find_index] != key,直接return false
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
  // 获取root_page
  Context ctx;
  InitContext(ctx, false);

  if (ctx.root_page_id_ == INVALID_PAGE_ID) {  // 若root_page不存在,新建root_page
    page_id_t new_page_id = bpm_->NewPage();
    // 初始化root_page
    WritePageGuard root_page_guard = bpm_->WritePage(new_page_id);
    auto root_page = root_page_guard.AsMut<LeafPage>();  // root_page初始为leafpage
    root_page->Init(leaf_max_size_);                     // 初始化leafpage
    root_page->InsertPairAt(0, key, value);              // 在root_page的0处插入键值对

    // 将root_page_id写入head_page
    ctx.read_set_.back().Drop();  // 需要释放header_page的read_guard，防止死锁
    ctx.read_set_.pop_back();
    WritePageGuard head_page_guard = bpm_->WritePage(header_page_id_);
    auto head_page = head_page_guard.AsMut<BPlusTreeHeaderPage>();
    head_page->root_page_id_ = new_page_id;

    return true;
  }

  // 获取key应在的leafpage
  WritePageGuard leaf_page_guard;
  OptSearchLeafPage<WritePageGuard>(key, ctx, leaf_page_guard);
  // 向leafpage插入键值对
  auto leaf_page = leaf_page_guard.AsMut<LeafPage>();
  int insert_id = KeyBinarySearch(leaf_page, key);           // 获取应插入的index
  if (comparator_(leaf_page->KeyAt(insert_id), key) == 0) {  // 若存在相同key,则返回false
    return false;
  }
  leaf_page->InsertPairAt(insert_id, key, value);

  // 向leaf_page插入后若size达到maxsize则进行分裂
  if (leaf_page->GetSize() == leaf_page->GetMaxSize()) {
    leaf_page_guard.Drop();  // 释放leaf_page_guard，防止进行分裂时死锁
    SplitLeafPage(key, ctx);
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
  // 获取root_page
  Context ctx;
  InitContext(ctx, false);

  if (ctx.root_page_id_ == INVALID_PAGE_ID) {  // 若数为空，直接返回
    return;
  }

  // 查找key应在的leafpage
  WritePageGuard leaf_page_guard;
  OptSearchLeafPage<WritePageGuard>(key, ctx, leaf_page_guard);
  auto leaf_page = leaf_page_guard.AsMut<LeafPage>();
  // 查找key是否存在于该leafpage
  int find_index = KeyBinarySearch(leaf_page, key);
  if (comparator_(leaf_page->KeyAt(find_index), key) != 0) {  // 若该key不存在于page,直接返回
    return;
  }

  // 进行删除
  leaf_page->RemovePairAt(find_index);

  // 向leaf_page删除后若size小于minsize则进行合并
  if (leaf_page->GetSize() < leaf_page->GetMinSize()) {
    MergePage<LeafPage>(ctx, key);
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
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  int begin_page_id = GetBEPageId(true);
  ReadPageGuard guard = bpm_->ReadPage(begin_page_id);
  // 返回最左边leaf_page的第0项
  return INDEXITERATOR_TYPE(begin_page_id, 0, bpm_);
}

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  Context ctx;
  InitContext(ctx, false);

  ReadPageGuard guard;
  OptSearchLeafPage<ReadPageGuard>(key, ctx, guard);
  auto page = guard.As<LeafPage>();
  int index = KeyBinarySearch(page, key);
  // 返回key所在的page_id和index
  return INDEXITERATOR_TYPE(guard.GetPageId(), index, bpm_);
}

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE {
  int end_page_id = GetBEPageId(false);
  ReadPageGuard guard = bpm_->ReadPage(end_page_id);
  auto page = guard.As<LeafPage>();
  int index = page->GetSize();
  // 返回最右边的leaf_page的最后一项的下一位；本次实现保证leaf_page为满就立刻split,故不可能有已满的leaf_page
  return INDEXITERATOR_TYPE(end_page_id, index, bpm_);
}

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
 * @brief 初始化ctx:将root_page_id存入ctx;将header_guard存入ctx.read_set或write_set
 *
 * @param is_write 表示是否以write_guard获取header_page
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InitContext(Context &ctx, bool is_write) {
  if (is_write) {
    WritePageGuard header_guard = bpm_->WritePage(header_page_id_);
    auto header_page = header_guard.As<BPlusTreeHeaderPage>();
    ctx.root_page_id_ = header_page->root_page_id_;
    ctx.write_set_.push_back(std::move(header_guard));
  } else {
    ReadPageGuard header_guard = bpm_->ReadPage(header_page_id_);
    auto header_page = header_guard.As<BPlusTreeHeaderPage>();
    ctx.root_page_id_ = header_page->root_page_id_;
    ctx.read_set_.push_back(std::move(header_guard));  // 添加root_guard
  }
}

/**
 * @brief 获取开始/结束的leaf_page_id
 * @param is_begin 表示寻找开始(true)或结束(end)的leaf_page
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetBEPageId(bool is_begin) const -> int {
  // 获取root_page_id
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto header_page = guard.As<BPlusTreeHeaderPage>();
  int now_page_id = header_page->root_page_id_;
  // 遍历到leaf_page
  ReadPageGuard now_guard = bpm_->ReadPage(now_page_id);
  auto judge = now_guard.As<BPlusTreePage>();
  while (!judge->IsLeafPage()) {
    auto now_page = now_guard.As<InternalPage>();
    if (is_begin) {  // 若查找开始的leaf_page
      now_page_id = now_page->ValueAt(0);
    } else {  // 若查找结束的leaf_page
      now_page_id = now_page->ValueAt(now_page->GetSize() - 1);
    }
    now_guard = bpm_->ReadPage(now_page_id);
    judge = now_guard.As<BPlusTreePage>();
  }
  return now_page_id;
}

/**
 * @brief 在page内的key_array进行二分查找key。
 *        在internalpage内查找index,使key_array[index] <= key < key_array[index + 1];若key<key_array[1]则返回0。
 *        在leafpage内查找index,若完全匹配则返回匹配值,否则返回index使key_array[index - 1] < key < key_array[index]。
 *        即两种page的所需算法大体一致，但不完全匹配时，leaf所需index=internal所需index + 1
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename PageType>
auto BPLUSTREE_TYPE::KeyBinarySearch(const PageType *page, const KeyType &key) const -> int {
  int left = 0;
  int right = page->GetSize() - 1;
  // 进行二分查找
  while (left <= right) {
    int mid_index = std::ceil((left + right) / 2.0);

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
 *        将res_guard移动赋值为找到的leafpage的guard
 *
 * @param ctx 传入时需保证ctx.read_set只有header_page,ctx.root_page_id有值;返回时ctx.read_set为空
 * @param res_guard 类型需与GuardType一致;返回时移动赋值为查找到的leafpage的guard
 *
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename GuardType>
void BPLUSTREE_TYPE::OptSearchLeafPage(const KeyType &key, Context &ctx, GuardType &res_guard) const {
  page_id_t now_page_id = ctx.root_page_id_;
  while (true) {
    // 读取该页
    ReadPageGuard guard = bpm_->ReadPage(now_page_id);

    auto judge = guard.As<BPlusTreePage>();  // 先转换为父类，判断是internalpage或leafpage
    if (judge->IsLeafPage()) {               // 若是leafpage,返回
      // 此时父页的锁仍未释放，故可以安全将leafpage从read_guard转换为write_guard
      if constexpr (std::is_same_v<GuardType, WritePageGuard>) {
        guard.Drop();  // 先释放read_guard，以便能获取WritePage
        res_guard = std::move(bpm_->WritePage(now_page_id));
      } else {
        res_guard = std::move(guard);
      }
      // 释放父页
      ctx.read_set_.pop_back();
      return;
    }
    // 若是internalpage，则继续查找
    auto now_page = guard.As<InternalPage>();
    int find_index = KeyBinarySearch(now_page, key);
    now_page_id = now_page->ValueAt(find_index);
    // 处理锁
    ctx.read_set_.pop_back();                   // 确定该页能被读取后再释放父页
    ctx.read_set_.push_back(std::move(guard));  // 加锁该页
  }
}

/**
 * @brief 查找key应当存在的leafpage
 *        使用悲观读取,即确认该页安全后释放查找路径上之前的latch
 *
 * @param ctx 传入时需保证ctx.write_set只有header_page,ctx.root_page_id有值;
 *            返回时ctx.write_set按序保存可能会被更改的page_guard,即最后一项为查找到的leaf_page的guard
 * @param is_split 表示是为split(true)还是merge(false)进行查找
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::PessSearchLeafPage(const KeyType &key, Context &ctx, bool is_split) const {
  page_id_t now_page_id = ctx.root_page_id_;

  // 查找key所在的leaf_page,在能确保安全后释放祖页锁
  while (true) {
    WritePageGuard guard = bpm_->WritePage(now_page_id);

    auto judge = guard.As<BPlusTreePage>();  // 先转换为父类，判断是internalpage或leafpage
    if (judge->IsLeafPage()) {               // 若是leafpage,返回
      ctx.write_set_.push_back(std::move(guard));
      return;
    }
    // 若是internalpage，则继续查找
    auto now_page = guard.As<InternalPage>();
    int find_index = KeyBinarySearch(now_page, key);
    now_page_id = now_page->ValueAt(find_index);

    // 处理锁
    if (is_split) {  // 若为了split查找
      if (now_page->GetSize() < now_page->GetMaxSize()) {
        // 一旦发现有internalpage未满，说明它本身不会被分裂，即说明其祖页不会被更改，可以直接释放祖页
        ctx.write_set_.clear();
      }
    } else {  // 若为了merge查找
      if (now_page->GetSize() - 1 >= now_page->GetMinSize()) {
        // 若发现internalpage减少一项后大于等于minsize,说明它本身不会合并，即说明其祖页不会被更改，可以直接释放祖页
        ctx.write_set_.clear();
      }
    }
    ctx.write_set_.push_back(std::move(guard));  // 加锁该页
  }
}

/**
 * @brief 获取给定page_id的page的size;is_leaf表示该page是否为leafpage
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetPageSizeById(const int page_id, bool is_leaf) const -> int {
  ReadPageGuard guard = bpm_->ReadPage(page_id);
  if (is_leaf) {
    auto page = guard.As<LeafPage>();
    return page->GetSize();
  } else {
    auto page = guard.As<InternalPage>();
    return page->GetSize();
  }
}

/**
 * @brief 获取给定page的next_page的id;若无next_page则返回nullopt
 * @param page_id leaf_page的id
 * @param ancestor_page_id page的祖页的id,这里采取复制传递
 */
// INDEX_TEMPLATE_ARGUMENTS
// auto BPLUSTREE_TYPE::FindNextPage(const int page_id, const std::deque<int> ancestor_page_id) const
//     -> std::optional<int> {
//   // TODO:利用BinarySearchRes进行重构后，就不需要每次父页都重新查找，直接利用之前查找结果即可
//   int parent_id = ancestor_page_id.back();
//   ReadPageGuard parent_guard = bpm_->ReadPage(parent_id);
//   int find_index = ;
// }

/**
 * @brief 分裂ctx.write_set_的最后一项对应的leaf_page,向父页插入新节点
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::SplitLeafPage(const KeyType &key, Context &ctx) {
  // TODO:插入时会莫名其妙缺少值
  // 初始化ctx，以ctx.write_set锁住header_page
  InitContext(ctx, true);

  // 悲观获取leafpage路径上的write_guard,ctx.write_set的最后一项为找到的leaf_page_guard
  PessSearchLeafPage(key, ctx, true);

  // 读取要被分裂的leafpage
  WritePageGuard old_leaf_page_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();
  auto old_leaf_page = old_leaf_page_guard.AsMut<LeafPage>();
  page_id_t old_leaf_page_id = old_leaf_page_guard.GetPageId();

  // 初始化新页
  page_id_t new_leaf_page_id = bpm_->NewPage();
  WritePageGuard new_leaf_page_guard = bpm_->WritePage(new_leaf_page_id);
  auto new_leaf_page = new_leaf_page_guard.AsMut<LeafPage>();
  new_leaf_page->Init(leaf_max_size_);

  // 将old_leaf_page一半以后的数据剪切到new_leaf_page
  old_leaf_page->SplitHalfPairTo(new_leaf_page);

  // 更新new_page和old_page的next_id
  new_leaf_page->SetNextPageId(old_leaf_page->GetNextPageId());
  new_leaf_page->SetPrePageId(old_leaf_page_id);
  old_leaf_page->SetNextPageId(new_leaf_page_id);

  // 获取新产生的节点的key
  KeyType new_key = new_leaf_page->KeyAt(0);  // new_leaf_page的第一项即为插入父页的key

  // 向leaf_page的父页进行插入
  if (ctx.IsRootPage(old_leaf_page_id)) {  // 若分裂的子页是root_page，则需要传入其page_id作为lvalue分裂root_page
    InsertPairToInternalPage(ctx, new_key, new_leaf_page_id, old_leaf_page_id);
  } else {  // 若分裂的子页不是root_page，正常插入
    InsertPairToInternalPage(ctx, new_key, new_leaf_page_id);
  }
}

/**
 * @brief (SplitLeafPage的辅助函数)向ctx.write_set_的最后一项插入键值对，若需要分裂则进行递归
 *
 * @param lvalue 默认lvalue是空值;若有值，则说明需要分裂root_page,将lvalue插入新root_page的0处
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertPairToInternalPage(Context &ctx, const KeyType &key, const page_id_t &value,
                                              std::optional<const page_id_t> lvalue) {
  /* 获取当前页 */
  page_id_t insert_page_id;
  WritePageGuard insert_page_guard;
  if (lvalue.has_value()) {  // 需要分裂root_page
    //  初始化新的root_page
    page_id_t new_root_page_id = bpm_->NewPage();
    insert_page_guard = bpm_->WritePage(new_root_page_id);
    auto new_root_page = insert_page_guard.AsMut<InternalPage>();
    new_root_page->Init(internal_max_size_);
    // 向新的root_page插入键值对:0处的值为lvalue,1处的键值对为(key, value)
    new_root_page->InsertPairAt(1, key, value);
    new_root_page->SetValueAt(0, lvalue.value());  // 在0处设置lvalue
    // 更新root_page_id
    WritePageGuard head_page_guard = std::move(ctx.write_set_.back());  // 此时write_set_内一定只有header_page
    auto head_page = head_page_guard.AsMut<BPlusTreeHeaderPage>();
    head_page->root_page_id_ = new_root_page_id;
    return;
  } else {  // 否则获取ancestor最后一项
    insert_page_guard = std::move(ctx.write_set_.back());
    insert_page_id = insert_page_guard.GetPageId();
    ctx.write_set_.pop_back();  // 将最后一项去除
  }

  /* 判断当前页是否需要分裂 */
  auto insert_page = insert_page_guard.AsMut<InternalPage>();
  if (insert_page->GetSize() == insert_page->GetMaxSize()) {  // 达到maxsize，需要递归分裂
    // 创建新页
    page_id_t new_page_id = bpm_->NewPage();
    WritePageGuard new_page_guard = bpm_->WritePage(new_page_id);
    auto new_page = new_page_guard.AsMut<InternalPage>();
    new_page->Init(internal_max_size_);
    // 将旧页一半以后的内容剪切到新页
    KeyType new_key = insert_page->SplitHalfPairTo(new_page);  // 返回值为插入下一级父页的key
    // 检查下次递归时是否需要传递lvalue
    if (ctx.IsRootPage(insert_page_id)) {  // 若本页是root_page，则需要传递now_page_id作为lvalue分裂root_page
      InsertPairToInternalPage(ctx, new_key, new_page_id, insert_page_id);
    } else {
      InsertPairToInternalPage(ctx, new_key, new_page_id);
    }

    // 分裂完成后，判断需要向新页还是旧页插入
    if (comparator_(key, new_key) >= 0) {  // 若要插入的key大于new_key,则向新页插入
      insert_page = new_page;
    }  // 否则向旧页插入，不需更改insert_page
  }

  /* 分裂完成后进行插入 */
  int insert_index = KeyBinarySearch(insert_page, key) + 1;  // 插入位置为internalpage查找位置+1
  insert_page->InsertPairAt(insert_index, key, value);
}

/**
 * @brief 合并ctx.write_set_的最后一项对应的页,从父页删除或修改一项键值对
 * @param PageType 表示待合并的页是leaf_page或internal_page
 * @param key 只有合并leaf_page时需要传值，方便进行悲观查找
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename PageType>
void BPLUSTREE_TYPE::MergePage(Context &ctx, const std::optional<KeyType> key) {
  /* 合并leaf_page时key有值，进行悲观查找 */
  if (key.has_value()) {
    // 初始化ctx
    InitContext(ctx, true);
    // 悲观获取leafpage路径上的write_guard
    PessSearchLeafPage(key.value(), ctx, false);
  }

  /* 获取now_page */
  WritePageGuard now_page_guard = std::move(ctx.write_set_.back());
  page_id_t now_page_id = now_page_guard.GetPageId();
  ctx.write_set_.pop_back();          // 去除ctx.write_set的最后一项
  if (ctx.IsRootPage(now_page_id)) {  // 若now_page为根页，则无需合并，直接结束
    return;
  }
  auto now_page = now_page_guard.AsMut<PageType>();

  /* 获取待合并的另一个now_page */
  // 获取父页
  WritePageGuard parent_page_guard = std::move(ctx.write_set_.back());
  auto parent_page = parent_page_guard.AsMut<InternalPage>();
  // 获取now_page的左右兄弟页,取size较大的进行merge
  int find_index = KeyBinarySearch(parent_page, now_page->KeyAt(0));
  int merge_leaf_page_id;  // 记录进行merge的page_id
  bool is_right_sibling;   // 记录获取的是否为右兄弟

  if (find_index == 0) {  // 若find_index为0，直接使用右兄弟
    merge_leaf_page_id = parent_page->ValueAt(find_index + 1);
    is_right_sibling = true;
  } else if (find_index == parent_page->GetSize() - 1) {  // 若find_index为最后一项，直接用左兄弟
    merge_leaf_page_id = parent_page->ValueAt(find_index - 1);
    is_right_sibling = false;
  } else {  // 否则取左右兄弟size较大的一个
    if (GetPageSizeById(parent_page->ValueAt(find_index - 1), true) >
        GetPageSizeById(parent_page->ValueAt(find_index + 1), true)) {
      merge_leaf_page_id = parent_page->ValueAt(find_index - 1);
      is_right_sibling = false;
    } else {
      merge_leaf_page_id = parent_page->ValueAt(find_index + 1);
      is_right_sibling = true;
    }
  }
  // 获取merge_leaf_page
  WritePageGuard merge_leaf_page_guard = bpm_->WritePage(merge_leaf_page_id);
  auto merge_leaf_page = merge_leaf_page_guard.AsMut<PageType>();

  /* 进行merge */
  // 返回值表示merge_leaf_page是否为空
  bool is_another_empty = now_page->MergePairFrom(merge_leaf_page, is_right_sibling);

  /* 在父页删除或修改键值对，若父页需要合并则进行合并 */
  if (!is_another_empty) {   // 若merge_leaf_page非空，则修改父页对应键，不需要判断父页是否合并
    if (is_right_sibling) {  // 若合并的是右兄弟，则修改对应键
      parent_page->SetKeyAt(find_index + 1, merge_leaf_page->KeyAt(0));
    } else {  // 若合并的是左兄弟，则修改对应键
      parent_page->SetKeyAt(find_index, now_page->KeyAt(0));
    }
  } else {                   // 若另一个页面为空，则删除父页键值对，需要判断父页是否合并
    if (is_right_sibling) {  // 若右兄弟为空，则删除对应键值对
      // 若删除的是leaf_page，则更新next_page_id和pre_page_id
      if constexpr (std::is_same_v<PageType, LeafPage>) {
        // 更新now_page的next
        now_page->SetNextPageId(merge_leaf_page->GetNextPageId());
        // 更新右兄弟的下一页的pre
        WritePageGuard next_guard = bpm_->WritePage(merge_leaf_page->GetNextPageId());
        auto next_page = next_guard.AsMut<LeafPage>();
        next_page->SetPrePageId(now_page_id);
      }
      // 删除对应键值对
      parent_page->RemovePairAt(find_index + 1);
    } else {  // 若左兄弟为空，则修改对应键值对
      // 若删除的是leaf_page，则更新next_page_id和pre_page_id
      if constexpr (std::is_same_v<PageType, LeafPage>) {
        // 更新now_page的pre
        now_page->SetPrePageId(merge_leaf_page->GetPrePageId());
        // 更新左兄弟的上一页的next
        WritePageGuard pre_guard = bpm_->WritePage(merge_leaf_page->GetPrePageId());
        auto pre_page = pre_guard.AsMut<LeafPage>();
        pre_page->SetNextPageId(now_page_id);
      }
      parent_page->RemovePairAt(find_index - 1);
    }
    // 若父页需要合并则进行合并
    if (parent_page->GetSize() < parent_page->GetMinSize()) {
      MergePage<InternalPage>(ctx);
    }
  }
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
