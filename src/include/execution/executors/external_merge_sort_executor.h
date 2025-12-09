//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.h
//
// Identification: src/include/execution/executors/external_merge_sort_executor.h
//
// Copyright (c) 2015-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "common/config.h"
#include "common/macros.h"
#include "execution/execution_common.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/sort_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * Page to hold the intermediate data for external merge sort.
 *
 * Only fixed-length data will be supported in Fall 2024.
 */
class SortPage {
 public:
  /**
   * TODO: Define and implement the methods for reading data from and writing data to the sort
   * page. Feel free to add other helper methods.
   */

  // 初始化
  void Init() { tuples_num_ = 0; }

  // 判断n个size为tuple_size的tuples能否插入sortpage中
  static auto canBeInsert(uint32_t tuple_size, int n) -> bool {
    return SORT_PAGE_HEADER_SIZE + n * (TUPLE_OFFSET_SIZE + tuple_size + sizeof(int32_t)) <= BUSTUB_PAGE_SIZE;
  }

  auto GetTupleNum() const -> uint16_t { return tuples_num_; }

  // 向tuples_的末尾插入tuple;若能插入则返回true，否则返回false
  auto PushBackTuple(const Tuple &tuple) -> bool {
    uint16_t end_offset = tuples_num_ == 0 ? BUSTUB_PAGE_SIZE : tuple_offset_[tuples_num_ - 1];
    // 由于SerilaizeTo会加上data.size()，故offset也要加上sizeof(int32_t)
    uint16_t offset = end_offset - (tuple.GetLength() + sizeof(int32_t));
    uint16_t exist_offset = SORT_PAGE_HEADER_SIZE + TUPLE_OFFSET_SIZE * (tuples_num_ - 1);
    if (offset < exist_offset) {  // 若插入后超出page范围,则返回false
      return false;
    }

    tuple.SerializeTo(tuples_ + offset);
    // 更新offset和num
    tuple_offset_[tuples_num_] = offset;
    tuples_num_++;
    return true;
  }

  // 获取第index个tuple
  auto GetIndexTuple(int index) const -> Tuple {
    BUSTUB_ENSURE(index < tuples_num_ && index >= 0, "SortPage error: wrong index");
    uint16_t offset = tuple_offset_[index];
    Tuple tuple;
    tuple.DeserializeFrom(tuples_ + offset);
    return tuple;
  }

 private:
  /**
   * TODO: Define the private members. You may want to have some necessary metadata for
   * the sort page before the start of the actual data.
   */

  /**
   * sort page的结构如下
   *  ----------------------------------------------------------------
   *  | HEADER | tuple_offset_ ... | ... FREE SPACE ... | ... tuples_ |
   *  ----------------------------------------------------------------
   */

  // header_size 取 sizeof(num_tuples_) = 2
  static constexpr uint64_t SORT_PAGE_HEADER_SIZE = 2;
  static constexpr uint64_t TUPLE_OFFSET_SIZE = sizeof(uint16_t);

  // 记录本页所存的tuple，保证按顺序储存；在page中从后往前存
  char tuples_[0];

  // 记录当前tuples_存了多少tuple
  uint16_t tuples_num_;

  // 记录对应tuple的第一位所在位置
  uint16_t tuple_offset_[0];
};

/**
 * A data structure that holds the sorted tuples as a run during external merge sort.
 * Tuples might be stored in multiple pages, and tuples are ordered both within one page
 * and across pages.
 */
class MergeSortRun {
 public:
  MergeSortRun() = default;
  MergeSortRun(std::vector<page_id_t> pages, BufferPoolManager *bpm) : pages_(std::move(pages)), bpm_(bpm) {}

  auto GetPageCount() -> size_t { return pages_.size(); }

  /** Iterator for iterating on the sorted tuples in one run. */
  class Iterator {
    friend class MergeSortRun;

   public:
    Iterator() = default;

    /**
     * Advance the iterator to the next tuple. If the current sort page is exhausted, move to the
     * next sort page.
     *
     * TODO: Implement this method.
     */
    auto operator++() -> Iterator & {
      // 获取当前页的下一个tuple
      tuple_index_++;

      auto page = page_guard_.As<SortPage>();
      if (tuple_index_ >= page->GetTupleNum()) {  // 若读完当前page，则获取下一个page
        page_index_++;
        if (page_index_ != run_->pages_.size()) {
          page_guard_ = run_->bpm_->ReadPage(run_->pages_[page_index_]);
        }
        tuple_index_ = 0;
      }
      return *this;
    }

    /**
     * Dereference the iterator to get the current tuple in the sorted run that the iterator is
     * pointing to.
     *
     * TODO: Implement this method.
     */
    auto operator*() -> Tuple {
      auto page = page_guard_.As<SortPage>();
      return page->GetIndexTuple(tuple_index_);
    }

    /**
     * Checks whether two iterators are pointing to the same tuple in the same sorted run.
     *
     * TODO: Implement this method.
     */
    auto operator==(const Iterator &other) const -> bool {
      return page_index_ == other.page_index_ && tuple_index_ == other.tuple_index_;
    }

    /**
     * Checks whether two iterators are pointing to different tuples in a sorted run or iterating
     * on different sorted runs.
     *
     * TODO: Implement this method.
     */
    auto operator!=(const Iterator &other) const -> bool {
      return page_index_ != other.page_index_ || tuple_index_ != other.tuple_index_;
    }

   private:
    explicit Iterator(const MergeSortRun *run, int page_index, int tuple_index) : run_(run) {
      page_index_ = page_index;
      tuple_index_ = tuple_index;
      if (page_index_ != run_->pages_.size()) {  // 只有不为end()时获取guard
        page_guard_ = run_->bpm_->ReadPage(run_->pages_[page_index]);
      }
    }

    /** The sorted run that the iterator is iterating on. */
    [[maybe_unused]] const MergeSortRun *run_;

    /**
     * TODO: Add your own private members here. You may want something to record your current
     * position in the sorted run. Also feel free to add additional constructors to initialize
     * your private members.
     */

    // 记录当前读取到第几个pages_
    uint32_t page_index_;
    // 记录当前page_guard
    ReadPageGuard page_guard_;
    // 记录当前读取到该page的第几个tuple
    uint32_t tuple_index_;
  };

  /**
   * Get an iterator pointing to the beginning of the sorted run, i.e. the first tuple.
   *
   * TODO: Implement this method.
   */
  auto Begin() -> Iterator { return Iterator(this, 0, 0); }

  /**
   * Get an iterator pointing to the end of the sorted run, i.e. the position after the last tuple.
   *
   * TODO: Implement this method.
   */
  auto End() -> Iterator {
    return Iterator(this, pages_.size(), 0);  // 读取到pages_的最后一项的下一项即为end
  }

 private:
  /** The page IDs of the sort pages that store the sorted tuples. */
  std::vector<page_id_t> pages_;
  /**
   * The buffer pool manager used to read sort pages. The buffer pool manager is responsible for
   * deleting the sort pages when they are no longer needed.
   */
  [[maybe_unused]] BufferPoolManager *bpm_;
};

/**
 * ExternalMergeSortExecutor executes an external merge sort.
 *
 * In Fall 2024, only 2-way external merge sort is required.
 */
template <size_t K>
class ExternalMergeSortExecutor : public AbstractExecutor {
 public:
  ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                            std::unique_ptr<AbstractExecutor> &&child_executor);

  /** Initialize the external merge sort */
  void Init() override;

  /**
   * Yield the next tuple from the external merge sort.
   * @param[out] tuple The next tuple produced by the external merge sort.
   * @param[out] rid The next tuple RID produced by the external merge sort.
   * @return `true` if a tuple was produced, `false` if there are no more tuples
   */
  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the external merge sort */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

 private:
  /** The sort plan node to be executed */
  const SortPlanNode *plan_;

  /** Compares tuples based on the order-bys */
  TupleComparator cmp_;

  /** TODO: You will want to add your own private members here. */
  std::unique_ptr<AbstractExecutor> child_executor_;

  // 记录排好序后的run
  MergeSortRun sort_page_run_;
  MergeSortRun::Iterator run_it_;

  bool is_empty_;

  BufferPoolManager *bpm_;
  SchemaRef child_schema_;
};

}  // namespace bustub
