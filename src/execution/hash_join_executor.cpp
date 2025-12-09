//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.cpp
//
// Identification: src/execution/hash_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/hash_join_executor.h"

namespace bustub {

HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for Fall 2024: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void HashJoinExecutor::Init() {
  // 初始化child相关信息
  left_child_->Init();
  right_child_->Init();
  left_schema_ = plan_->GetLeftPlan()->output_schema_;
  right_schema_ = plan_->GetRightPlan()->output_schema_;

  // 初始化join_schema_
  std::vector<Column> tem_colum{left_schema_->GetColumns()};
  tem_colum.insert(tem_colum.end(), right_schema_->GetColumns().begin(), right_schema_->GetColumns().end());
  Schema tem_schema(tem_colum);
  join_schema_ = std::make_shared<Schema>(tem_schema);

  // 对于right_child构建hash表
  Tuple right_tuple;
  RID tem_rid;
  while (right_child_->Next(&right_tuple, &tem_rid)) {
    hash_map_[MakeGroupByKey(&right_tuple, false)].push_back(right_tuple);
  }
}

auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (true) {
    if (match_res_.empty()) {  // 若match_res_为空，则匹配下一个left_tuple
      auto status = left_child_->Next(&left_tuple_, rid);
      if (!status) {  // 若left_child已取尽，则返回false
        return false;
      }

      // 获取匹配结果
      match_res_ = hash_map_[MakeGroupByKey(&left_tuple_, true)];

      // 若是left_join且未匹配到，则返回right_tuple为null的tuple
      if (plan_->join_type_ == JoinType::LEFT && match_res_.empty()) {
        std::vector<Value> lvalues(GetAllValueFromTuple(left_tuple_, *left_schema_, false));
        std::vector<Value> rvalues(GetAllValueFromTuple(Tuple(), *right_schema_, true));
        lvalues.insert(lvalues.end(), rvalues.begin(), rvalues.end());
        *tuple = Tuple(lvalues, join_schema_.get());
        return true;
      }
    } else {
      // 获取right_tuple
      Tuple right_tuple = match_res_.back();
      match_res_.pop_back();
      // 返回join后的结果
      std::vector<Value> lvalues(GetAllValueFromTuple(left_tuple_, *left_schema_, false));
      std::vector<Value> rvalues(GetAllValueFromTuple(right_tuple, *right_schema_, false));
      lvalues.insert(lvalues.end(), rvalues.begin(), rvalues.end());
      *tuple = Tuple(lvalues, join_schema_.get());
      return true;
    }
  }
}

auto HashJoinExecutor::MakeGroupByKey(const Tuple *tuple, bool is_left) -> AggregateKey {
  std::vector<Value> keys;

  // 获取对应plan和schema
  std::vector<AbstractExpressionRef> plans;
  SchemaRef schema;
  if (is_left) {
    plans = plan_->LeftJoinKeyExpressions();
    schema = left_schema_;
  } else {
    plans = plan_->RightJoinKeyExpressions();
    schema = right_schema_;
  }

  keys.resize(keys.size());
  for (const auto &expr : plans) {
    keys.emplace_back(expr->Evaluate(tuple, *schema));
  }
  return {keys};
}

auto HashJoinExecutor::GetAllValueFromTuple(const Tuple &tuple, const Schema &schema, bool is_null) const
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
