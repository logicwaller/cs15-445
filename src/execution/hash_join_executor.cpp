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
  match_res_index_ = 0;
  match_res_ = nullptr;

  // 对于right_child构建hash表
  Tuple right_tuple;
  RID tem_rid;
  while (right_child_->Next(&right_tuple, &tem_rid)) {
    hash_map_[MakeGroupByKey(&right_tuple, plan_->RightJoinKeyExpressions(), right_schema_)].push_back(right_tuple);
  }

  // 建立bloom过滤器
  bloom_ = std::make_unique<BloomFilter>(BloomFilter(hash_map_.size(), 2));
  for (const auto &k : hash_map_) {
    bloom_->Insert(k.first);
  }
}

auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (true) {
    if (match_res_ == nullptr || match_res_index_ >= match_res_->size()) {  // 若match_res_为空，则匹配下一个left_tuple
      auto status = left_child_->Next(&left_tuple_, rid);
      if (!status) {  // 若left_child已取尽，则返回false
        return false;
      }

      const auto &left_aggkey = MakeGroupByKey(&left_tuple_, plan_->LeftJoinKeyExpressions(), left_schema_);
      bool not_contain = false;  // 检查是否在bloom过滤器就能过滤当前left_tuple
      if (!bloom_->PossiblyContains(left_aggkey)) {
        not_contain = true;
        match_res_ = nullptr;
      } else {
        // 获取匹配结果
        auto it = hash_map_.find(left_aggkey);
        if (it == hash_map_.end()) {
          not_contain = true;
          match_res_ = nullptr;
        } else {
          match_res_ = &it->second;
          match_res_index_ = 0;
        }
      }
      // 若无法通过bloom 或 left_join且未匹配，则返回right_tuple为null的tuple
      if (plan_->join_type_ == JoinType::LEFT && (not_contain || match_res_->empty())) {
        const auto &new_values = CombineTwoTuple(left_tuple_, *left_schema_, Tuple(), *right_schema_, true);
        *tuple = Tuple(new_values, &plan_->OutputSchema());
        return true;
      }
    } else {
      // 获取right_tuple
      const Tuple &right_tuple = match_res_->at(match_res_index_++);
      // 返回join后的结果
      const auto &new_values = CombineTwoTuple(left_tuple_, *left_schema_, right_tuple, *right_schema_, false);
      *tuple = Tuple(new_values, &plan_->OutputSchema());
      return true;
    }
  }
}

auto HashJoinExecutor::MakeGroupByKey(const Tuple *tuple, const std::vector<AbstractExpressionRef> &plans,
                                      const SchemaRef &schema) -> AggregateKey {
  std::vector<Value> keys;
  keys.reserve(plans.size());
  for (const auto &expr : plans) {
    keys.emplace_back(expr->Evaluate(tuple, *schema));
  }
  return {keys};
}

auto HashJoinExecutor::CombineTwoTuple(const Tuple &ltuple, const Schema &lschema, const Tuple &rtuple,
                                       const Schema &rschema, bool is_right_null) -> std::vector<Value> {
  std::vector<Value> res;
  uint32_t lcolumn_size = lschema.GetColumnCount();
  uint32_t rcolumn_size = rschema.GetColumnCount();
  res.reserve(lcolumn_size + rcolumn_size);
  // 插入left_tuple
  for (uint32_t i = 0; i < lcolumn_size; i++) {
    res.emplace_back(ltuple.GetValue(&lschema, i));
  }
  // 插入right_tuple
  for (uint32_t i = 0; i < rcolumn_size; i++) {
    if (!is_right_null) {
      res.emplace_back(rtuple.GetValue(&rschema, i));
    } else {
      res.emplace_back(ValueFactory::GetNullValueByType(rschema.GetColumn(i).GetType()));
    }
  }
  return res;
}

}  // namespace bustub
