//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"

namespace bustub {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
  iterator_ =
      std::make_unique<TableIterator>(exec_ctx_->GetCatalog()->GetTable(plan_->table_name_)->table_->MakeIterator());
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // 通过iterator遍历返回所有tuple
  while (true) {
    if (iterator_->IsEnd()) {
      return false;
    }

    auto pair = iterator_->GetTuple();
    ++*iterator_;

    if (!pair.first.is_deleted_) {  // 若没被删则返回该tuple
      *tuple = pair.second;
      *rid = tuple->GetRid();

      if (plan_->filter_predicate_ != nullptr) {  // 若filter存在，检验该tuple是否能通过filter
        Value value = plan_->filter_predicate_->Evaluate(tuple, GetOutputSchema());
        if (value.CompareEquals(Value(TypeId::BOOLEAN, 1)) == CmpBool::CmpTrue) {
          // 若能通过fliter则返回
          return true;
        }
      } else {  // 若filter不存在，则直接返回true
        return true;
      }
    }
  }
}

}  // namespace bustub
