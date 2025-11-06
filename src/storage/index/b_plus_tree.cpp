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
  return header_page->root_page_id_ == INVALID_PAGE_ID;  // 若root_page没有东西则为空
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
  if (!leaf_page->IsIndexValid(find_index) ||
      comparator_(leaf_page->KeyAt(find_index), key) != 0) {  // 若不存在该key,直接return false
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
  int insert_id = KeyBinarySearch(leaf_page, key);  // 获取应插入的index
  if (leaf_page->IsIndexValid(insert_id) &&
      comparator_(leaf_page->KeyAt(insert_id), key) == 0) {  // 若存在相同key,则返回false
    return false;
  }
  leaf_page->InsertPairAt(insert_id, key, value);

  // 向leaf_page插入后若size达到maxsize则进行分裂
  if (leaf_page->GetSize() == leaf_page->GetMaxSize()) {
    leaf_page_guard.Drop();  // 释放leaf_page_guard，防止进行分裂时死锁
    // TODO:这里是否应该释放leaf_page_guard，如果释放的话不能确保splitpage执行的时候这个页还需要分裂
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
  if (!leaf_page->IsIndexValid(find_index) ||
      comparator_(leaf_page->KeyAt(find_index), key) != 0) {  // 若该key不存在于page,直接返回
    return;
  }

  // 进行删除
  leaf_page->RemovePairAt(find_index);

  // 处理合并
  if (ctx.IsRootPage(leaf_page_guard.GetPageId())) {
    if (leaf_page->GetSize() == 0) {  // 当根页为leafpage且该页被删光，则设置根页id为invalid
      WritePageGuard header_guard = bpm_->WritePage(header_page_id_);
      auto header_page = header_guard.AsMut<BPlusTreeHeaderPage>();
      header_page->root_page_id_ = INVALID_PAGE_ID;
    }
  } else if (leaf_page->GetSize() < leaf_page->GetMinSize()) {  // 向leaf_page删除后若size小于minsize则进行合并
    leaf_page_guard.Drop();                                     // 释放leaf_page_guard，防止进行分裂时死锁
    // TODO:这里是否应该释放leaf_page_guard，如果释放的话不能确保mergepage执行的时候这个页还需要合并
    MergePage(ctx, key);
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
 * @return 返回查找路径上找到的子页在该页的index，最后一项为leafpage的父页查找的leafpage所在位置的index
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::PessSearchLeafPage(const KeyType &key, Context &ctx, bool is_split) const -> std::deque<int> {
  page_id_t now_page_id = ctx.root_page_id_;
  std::deque<int> res_index;

  // 查找key所在的leaf_page,在能确保安全后释放祖页锁
  while (true) {
    WritePageGuard guard = bpm_->WritePage(now_page_id);

    auto judge = guard.As<BPlusTreePage>();  // 先转换为父类，判断是internalpage或leafpage
    if (judge->IsLeafPage()) {               // 若是leafpage,返回
      // 更新write_set
      ctx.write_set_.push_back(std::move(guard));
      return res_index;
    }
    // 若是internalpage，则继续查找
    auto now_page = guard.As<InternalPage>();
    int find_index = KeyBinarySearch(now_page, key);
    now_page_id = now_page->ValueAt(find_index);
    res_index.push_back(find_index);  // 插入res_index

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
    ctx.write_set_.pop_back();  // 将最后一项去除
    insert_page_id = insert_page_guard.GetPageId();
  }

  /* 判断当前页是否需要分裂 */
  auto insert_page = insert_page_guard.AsMut<InternalPage>();
  if (insert_page->GetSize() == insert_page->GetMaxSize()) {  // 达到maxsize，需要分裂
    // 创建新页
    page_id_t new_page_id = bpm_->NewPage();
    WritePageGuard new_page_guard = bpm_->WritePage(new_page_id);
    auto new_page = new_page_guard.AsMut<InternalPage>();
    new_page->Init(internal_max_size_);

    // 将旧页一半以后的内容剪切到新页
    KeyType new_key;  // 记录插入下一级父页的key
    int min_size = insert_page->GetMinSize();
    // 根据key和min_size_key的大小决定截断位置，方便均衡页大小
    if (comparator_(key, insert_page->KeyAt(min_size)) >= 0) {
      // key >= key_array[min_size]，则在min_size处截断insert_page，key插入新页
      new_key = insert_page->SplitHalfPairTo(new_page, min_size);
      insert_page = new_page;
    } else if (comparator_(key, insert_page->KeyAt(min_size - 1)) < 0) {
      // key < key_array[min_size - 1], 在min_size-1处截断insert_page, key插入旧页
      new_key = insert_page->SplitHalfPairTo(new_page, min_size - 1);
    } else {
      // key_array[min_size - 1] <= key < key_array[min_size]，则在min_size处截断，key插入父页，将value插入新页的第0项
      insert_page->SplitHalfPairTo(new_page, min_size, value);
      new_key = key;
      insert_page = nullptr;
    }

    // 检查下次递归插入父页时是否需要传递lvalue
    if (ctx.IsRootPage(insert_page_id)) {  // 若本页是root_page，则需要传递now_page_id作为lvalue分裂root_page
      InsertPairToInternalPage(ctx, new_key, new_page_id, insert_page_id);
    } else {
      InsertPairToInternalPage(ctx, new_key, new_page_id);
    }
  }

  /* 分裂完成后进行插入 */
  if (insert_page != nullptr) {
    int insert_index = KeyBinarySearch(insert_page, key) + 1;  // 插入位置为internalpage查找位置+1
    insert_page->InsertPairAt(insert_index, key, value);
  }
}

/**
 * @brief 合并key所在的leaf_page，初始化ctx
 *        不像split那样分开leaf_page和internal_page写成两个函数是因为合并时两种页的处理情况十分相似
 * @param key 悲观查找可能所需要合并的页
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::MergePage(Context &ctx, const KeyType &key) {
  // 初始化ctx
  InitContext(ctx, true);
  // 悲观获取leafpage路径上的write_guard, 返回每页查找项的index
  std::deque<int> res_index = PessSearchLeafPage(key, ctx, false);
  MergePageHelper<LeafPage>(ctx, res_index);
}

/**
 * @brief 合并ctx.write_set_的最后一项对应的页,从父页删除或修改一项键值对
 * @param PageType 表示待合并的页是leaf_page或internal_page
 * @param res_index 表示每一页查找项的index
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename PageType>
void BPLUSTREE_TYPE::MergePageHelper(Context &ctx, std::deque<int> &res_index) {
  /* 获取now_page */
  WritePageGuard now_page_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();  // 去除ctx.write_set的最后一项
  page_id_t now_page_id = now_page_guard.GetPageId();
  if (ctx.IsRootPage(now_page_id)) {  // 若now_page为根页，则判断是否需要减高度
    auto root_page = now_page_guard.As<InternalPage>();
    // 此时write_set一定只剩下header_page
    WritePageGuard header_page_guard = std::move(ctx.write_set_.back());
    auto header_page = header_page_guard.AsMut<BPlusTreeHeaderPage>();
    if (root_page->GetSize() == 1) {  // 若root_page只剩一个value，则直接改变root_page_id为该value即可
      header_page->root_page_id_ = root_page->ValueAt(0);
    }
    return;
  }
  auto now_page = now_page_guard.AsMut<PageType>();

  /* 获取待合并的另一个now_page */
  // 获取父页
  WritePageGuard parent_page_guard = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();  // 暂时去除最后一项，之后若用到会加回
  auto parent_page = parent_page_guard.AsMut<InternalPage>();

  // 获取now_page的左右兄弟页,取size较大的进行merge
  int find_index = res_index.back();  // 获取now_page的父页中存储now_page的index
  res_index.pop_back();
  bool is_right_sibling;  // 记录获取的是否为右兄弟
  int merge_page_id;      // 记录进行merge的page_id
  KeyType insert_key;     // 记录在merge internal_page时新加入的key;merge leaf_page时无用

  if (find_index == 0) {  // 若find_index为0，直接使用右兄弟
    is_right_sibling = true;
  } else if (find_index == parent_page->GetSize() - 1) {  // 若find_index为最后一项，直接用左兄弟
    is_right_sibling = false;
  } else {  // 否则取左右兄弟size较大的一个
    if (GetPageSizeById(parent_page->ValueAt(find_index - 1), true) >
        GetPageSizeById(parent_page->ValueAt(find_index + 1), true)) {
      is_right_sibling = false;
    } else {
      is_right_sibling = true;
    }
  }

  if (is_right_sibling) {  // 合并右兄弟时，需要在本页插入父页对应的key
    merge_page_id = parent_page->ValueAt(find_index + 1);
    insert_key = parent_page->KeyAt(find_index + 1);
  } else {  // 合并左兄弟同理
    merge_page_id = parent_page->ValueAt(find_index - 1);
    insert_key = parent_page->KeyAt(find_index);
  }
  // 获取merge_page
  WritePageGuard merge_page_guard = bpm_->WritePage(merge_page_id);
  auto merge_page = merge_page_guard.AsMut<PageType>();

  /* 进行merge */
  // 返回值表示merge_leaf_page是否为空
  bool is_another_empty;
  if constexpr (std::is_same_v<PageType, LeafPage>) {
    is_another_empty = now_page->MergePairFrom(merge_page, is_right_sibling);
  } else {  // 只有internal_page时需要传入insert_key
    is_another_empty = now_page->MergePairFrom(merge_page, is_right_sibling, insert_key);
  }

  /* 在父页删除或修改键值对，若父页需要合并则进行合并 */
  if (!is_another_empty) {  // 若merge_leaf_page非空，则修改父页对应键，不需要判断父页是否合并
    // internal_page在合并后保证keyAt(0)是有效值，表示可分割now_page和merge_page的key
    if (is_right_sibling) {  // 若合并的是右兄弟，则修改对应键
      parent_page->SetKeyAt(find_index + 1, merge_page->KeyAt(0));
    } else {  // 若合并的是左兄弟，则修改对应键
      parent_page->SetKeyAt(find_index, now_page->KeyAt(0));
    }
  } else {  // 若另一个页面为空，则删除父页键值对，需要判断父页是否合并
    // TODO:更新next_page_id会有并发问题，不一定能保证next_page或pre_page是安全的
    // 若删除的是leaf_page，则更新next_page_id和pre_page_id
    if constexpr (std::is_same_v<PageType, LeafPage>) {
      if (is_right_sibling) {
        // 更新now_page的next
        int next_page_id = merge_page->GetNextPageId();
        now_page->SetNextPageId(next_page_id);
        // 更新右兄弟的下一页的pre
        if (next_page_id != INVALID_PAGE_ID) {
          WritePageGuard next_guard = bpm_->WritePage(next_page_id);
          auto next_page = next_guard.AsMut<LeafPage>();
          next_page->SetPrePageId(now_page_id);
        }
      } else {
        // 更新now_page的pre
        int pre_page_id = merge_page->GetPrePageId();
        now_page->SetPrePageId(pre_page_id);
        // 更新左兄弟的上一页的next
        if (pre_page_id != INVALID_PAGE_ID) {
          WritePageGuard pre_guard = bpm_->WritePage(pre_page_id);
          auto pre_page = pre_guard.AsMut<LeafPage>();
          pre_page->SetNextPageId(now_page_id);
        }
      }
    }

    // 删除对应键值对
    if (is_right_sibling) {  // 若now_page的右兄弟清空，删除其记录
      parent_page->RemovePairAt(find_index + 1);
    } else {  // 若找到的是now_page的左兄弟，将本页在父页的键变为左兄弟的键再删除其记录
      parent_page->SetKeyAt(find_index, parent_page->KeyAt(find_index - 1));
      parent_page->RemovePairAt(find_index - 1);
    }

    // 若父页需要合并则进行合并
    if (parent_page->GetSize() < parent_page->GetMinSize()) {
      ctx.write_set_.push_back(std::move(parent_page_guard));  // 在write_set加回父页进行递归
      MergePageHelper<InternalPage>(ctx, res_index);
    }
  }
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
