//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// aggregation_executor.cpp
//
// Identification: src/execution/aggregation_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>
#include <vector>

#include "execution/executors/aggregation_executor.h"

namespace bustub {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                         std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      aht_(plan_->GetAggregates(), plan_->GetAggregateTypes()),
      aht_iterator_(aht_.Begin()) {}

void AggregationExecutor::Init() {
  // 初始化child_executor_
  child_executor_->Init();
  // 构建aht
  if (aht_.Begin() != aht_.End()) {  // 若aht不为空，则说明已执行过初始化，直接取aht.begin即可
    aht_iterator_ = aht_.Begin();
  } else {
    while (true) {
      Tuple child_tuple{};
      RID rid{};  // 不会被用到，随便一个rid即可
      auto status = child_executor_->Next(&child_tuple, &rid);
      if (!status) {
        if (aht_.Begin() == aht_.End() && plan_->GetGroupBys().empty()) {
          aht_.InitKey();  // 在表为空且没有group_by时添加初始化数据
        }
        aht_iterator_ = aht_.Begin();
        break;
      }
      aht_.InsertCombine(MakeAggregateKey(&child_tuple), MakeAggregateValue(&child_tuple));
    }
  }
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (aht_iterator_ == aht_.End()) {
    return false;
  }
  std::vector<Value> res;
  res.reserve(aht_iterator_.Key().group_bys_.size() + aht_iterator_.Val().aggregates_.size());
  res.insert(res.end(), aht_iterator_.Key().group_bys_.begin(), aht_iterator_.Key().group_bys_.end());
  res.insert(res.end(), aht_iterator_.Val().aggregates_.begin(), aht_iterator_.Val().aggregates_.end());
  ++aht_iterator_;
  *tuple = Tuple(res, &plan_->OutputSchema());
  return true;
}

auto AggregationExecutor::GetChildExecutor() const -> const AbstractExecutor * { return child_executor_.get(); }

}  // namespace bustub
