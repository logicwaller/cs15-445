//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.cpp
//
// Identification: src/execution/external_merge_sort_executor.cpp
//
// Copyright (c) 2015-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/external_merge_sort_executor.h"
#include <iostream>
#include <optional>
#include <vector>
#include "common/config.h"
#include "execution/plans/sort_plan.h"

namespace bustub {

template <size_t K>
ExternalMergeSortExecutor<K>::ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                                                        std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), cmp_(plan->GetOrderBy()), child_executor_(std::move(child_executor)) {}

template <size_t K>
void ExternalMergeSortExecutor<K>::Init() {
  /* 初始化各个值 */
  is_empty_ = false;
  // 初始化child_exec_
  child_executor_->Init();
  // 获取bpm
  bpm_ = exec_ctx_->GetBufferPoolManager();
  // 获取child_schema_
  child_schema_ = plan_->GetChildPlan()->output_schema_;

  /* 进行并归排序 */
  // 将child_exec的tuple插入sort_page中
  std::vector<MergeSortRun> runs;         // 记录归并排序的run
  if (!SortChildTupleToSortPage(runs)) {  // 若一个tuple都未插入，则返回
    is_empty_ = true;
    return;
  }

  // 对于sort_pages进行归并排序
  sort_page_run_ = ExecMergeSort(runs);  // 记录排序结果
  run_it_ = sort_page_run_.Begin();
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::Next(Tuple *tuple, RID *rid) -> bool {
  if (is_empty_ || run_it_ == sort_page_run_.End()) {  // 若child_exec未返回任何tuple或已遍历完，则返回false
    return false;
  }

  *tuple = *run_it_;
  ++run_it_;
  return true;
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::SortChildTupleToSortPage(std::vector<MergeSortRun> &runs) -> bool {
  TupleComparator comp(plan_->GetOrderBy());  // 记录比较器
  // 先将child_exec_内的所有page全读入sort_page中
  Tuple tuple;
  RID rid;
  std::vector<SortEntry> sorted_tuples;  // 记录排好序的tuple
  bool is_end = false;                   // 记录child_exec_是否已遍历完
  while (true) {
    // 若插入当前tuple后就超出一个sortpage的容量，则将现有的tuple插入
    if (!SortPage::CanBeInsert(tuple.GetLength(), sorted_tuples.size() + 1) || is_end) {
      // 将tuple排序
      std::sort(sorted_tuples.begin(), sorted_tuples.end(), comp);
      // 新建sort_page
      page_id_t sort_page_id = bpm_->NewPage();
      WritePageGuard sort_page_guard = bpm_->WritePage(sort_page_id);
      auto sort_page = sort_page_guard.AsMut<SortPage>();
      sort_page->Init();
      // 将tuple插入sort_page
      for (const auto &tuple : sorted_tuples) {
        sort_page->PushBackTuple(tuple.second);
      }
      // 插入runs
      runs.push_back(MergeSortRun(std::vector<page_id_t>{sort_page_id}, bpm_));
      // 若child_exec已遍历完，则break
      if (is_end) {
        break;
      }

      // 更新sorted_tuples
      sorted_tuples.clear();
    }
    // 插入当前tuple
    if (child_executor_->Next(&tuple, &rid)) {
      sorted_tuples.emplace_back(GenerateSortKey(tuple, plan_->GetOrderBy(), *child_schema_), tuple);
    } else {
      is_end = true;
    }
  }

  // 若只有一个run且其sorted_tuples为空则说明未插入tuple,返回false
  return !(runs.size() == 1 && sorted_tuples.empty());
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::ExecMergeSort(std::vector<MergeSortRun> &runs) -> MergeSortRun {
  TupleComparator comp(plan_->GetOrderBy());  // 记录比较器
  while (runs.size() != 1) {
    std::vector<MergeSortRun> next_run;  // 记录下一轮归并的runs
    int s = runs.size();
    for (int i = 0; i < s; i += 2) {
      MergeSortRun &left = runs[i];
      if (i + 1 >= s) {  // 若最后一个run没有可供归并的下一个run，则直接插入next_run
        next_run.push_back(left);
        break;
      }
      MergeSortRun &right = runs[i + 1];

      // 对left和right进行归并排序
      auto left_it = left.Begin();
      auto right_it = right.Begin();
      page_id_t new_page_id = bpm_->NewPage();
      WritePageGuard new_page_guard = bpm_->WritePage(new_page_id);
      auto new_page = new_page_guard.AsMut<SortPage>();
      std::vector<page_id_t> pages{new_page_id};  // 将新的sort_page_id存入
      while (true) {
        // 获取插入的tuple
        Tuple insert_tuple;
        if (right_it == right.End()) {  // 若有一方为空则遍历另一方
          if (left_it == left.End()) {
            break;
          }
          insert_tuple = *left_it;
          ++left_it;
        } else if (left_it == left.End()) {
          if (right_it == right.End()) {
            break;
          }
          insert_tuple = *right_it;
          ++right_it;
        } else {
          SortKey left_key = GenerateSortKey(*left_it, plan_->GetOrderBy(), *child_schema_);
          SortKey right_key = GenerateSortKey(*right_it, plan_->GetOrderBy(), *child_schema_);
          if (comp({left_key, *left_it}, {right_key, *right_it})) {
            insert_tuple = *left_it;
            ++left_it;
          } else {
            insert_tuple = *right_it;
            ++right_it;
          }
        }

        // 进行插入，若无法插入则新建sort_page
        if (!new_page->PushBackTuple(insert_tuple)) {
          new_page_id = bpm_->NewPage();
          new_page_guard = bpm_->WritePage(new_page_id);
          new_page = new_page_guard.AsMut<SortPage>();
          pages.push_back(new_page_id);
          // 将tuple插入新的sort_page
          new_page->PushBackTuple(insert_tuple);
        }
      }

      // 生成并插入新的run
      next_run.emplace_back(MergeSortRun(pages, bpm_));
    }
    // 遍历next_run
    runs = next_run;
  }
  // 返回runs的唯一一个值
  return runs.back();
}

template class ExternalMergeSortExecutor<2>;

}  // namespace bustub
