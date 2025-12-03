//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include "binder/table_ref/bound_join_ref.h"
#include "common/exception.h"

namespace bustub {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_executor_(std::move(left_executor)),
      right_executor_(std::move(right_executor)),
      join_schema_(plan_->InferJoinSchema(*plan_->GetLeftPlan(), *plan_->GetRightPlan())) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2023 Fall: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestedLoopJoinExecutor::Init() {
  // 初始化左右child的信息
  left_executor_->Init();
  right_executor_->Init();
  left_schema_ = plan_->GetLeftPlan()->output_schema_;
  right_schema_ = plan_->GetRightPlan()->output_schema_;
  // 初始化左右child的tuple
  RID tem{};  // 传递空的RID
  left_status_ = left_executor_->Next(&left_tuple_, &tem);
  has_matched_ = false;
}

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  Tuple right_tuple;
  while (true) {
    // 先检验左child是否遍历结束，若是则直接返回false
    if (!left_status_) {
      return false;
    }

    // 遍历右child,检验是否有能匹配的tuple
    auto rstatus = right_executor_->Next(&right_tuple, rid);
    if (!rstatus) {  // 若遍历所有的right_child都无法匹配
      if (plan_->GetJoinType() == JoinType::LEFT &&
          !has_matched_) {  // 若是left_join且未匹配过，则返回right_tuple为null的tuple
        std::vector<Value> lvalues(GetAllValueFromTuple(left_tuple_, *left_schema_, false));
        std::vector<Value> rvalues(GetAllValueFromTuple(right_tuple, *right_schema_, true));
        lvalues.insert(lvalues.end(), rvalues.begin(), rvalues.end());
        *tuple = Tuple(lvalues, &join_schema_);

        // 重置has_matched_,设为true使得下一次循环会直接取left_exec的下一个tuple
        has_matched_ = true;
        return true;
      }

      // 取left_exec的下一个tuple
      left_status_ = left_executor_->Next(&left_tuple_, rid);
      has_matched_ = false;  // 重置has_matched_
      // 重置right_executor_
      right_executor_->Init();
    } else {
      // 判断是否匹配
      auto compare = plan_->predicate_->EvaluateJoin(&left_tuple_, *left_schema_, &right_tuple, *right_schema_);
      if (!compare.IsNull() && compare.GetAs<bool>()) {  // 若匹配，则返回对应tuple
        std::vector<Value> lvalues(GetAllValueFromTuple(left_tuple_, *left_schema_, false));
        std::vector<Value> rvalues(GetAllValueFromTuple(right_tuple, *right_schema_, false));
        lvalues.insert(lvalues.end(), rvalues.begin(), rvalues.end());
        *tuple = Tuple(lvalues, &join_schema_);

        // 记录本次left_tuple_已被匹配过
        has_matched_ = true;
        return true;
      }
    }
  }
}

auto NestedLoopJoinExecutor::GetAllValueFromTuple(const Tuple &tuple, const Schema &schema, bool is_null) const
    -> std::vector<Value> {
  std::vector<Value> res;
  if (!is_null) {
    uint32_t column_size = schema.GetColumnCount();
    for (uint32_t i = 0; i < column_size; i++) {
      res.push_back(tuple.GetValue(&schema, i));
    }
  } else {
    for (const auto &col : schema.GetColumns()) {
      res.push_back(ValueFactory::GetNullValueByType(col.GetType()));
    }
  }
  return res;
}

}  // namespace bustub
