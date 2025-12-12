//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.cpp
//
// Identification: src/execution/nested_index_join_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_index_join_executor.h"

namespace bustub {

NestIndexJoinExecutor::NestIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                                             std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2023 Spring: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestIndexJoinExecutor::Init() {
  // 初始化catalog
  index_info_ = exec_ctx_->GetCatalog()->GetIndex(plan_->GetIndexOid());
  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->GetInnerTableOid());
  // 初始化child_executor_
  child_executor_->Init();
  // 初始化left_schema_
  left_schema_ = plan_->GetChildPlan()->output_schema_;
}

auto NestIndexJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (true) {
    if (match_res_.empty()) {  // 若match_res为空，则寻找并进行匹配
      // 获取下一个left_tuple
      bool left_status = child_executor_->Next(&left_tuple_, rid);
      if (!left_status) {  // 若left_child已遍历结束，则返回false
        return false;
      }

      // 在left_tuple获取对应的key
      Value key = plan_->key_predicate_->Evaluate(&left_tuple_, *left_schema_);
      Schema key_schema(std::vector<Column>{key.GetColumn()});
      Tuple key_tuple(std::vector<Value>{key}, &key_schema);
      // 在index内查找匹配key的RID
      index_info_->index_->ScanKey(key_tuple, &match_res_, exec_ctx_->GetTransaction());

      if (plan_->GetJoinType() == JoinType::LEFT &&
          match_res_.empty()) {  // 若是left_join且未匹配过，则返回right_tuple为null的tuple
        auto new_values = CombineTwoTuple(left_tuple_, *left_schema_, Tuple(), plan_->InnerTableSchema(), true);
        *tuple = Tuple(new_values, &plan_->OutputSchema());
        return true;
      }
    } else {
      // 获取match_res_中的rid
      RID rid = match_res_.back();
      match_res_.pop_back();
      auto right_tuple = table_info_->table_->GetTuple(rid).second;
      // 返回join后的结果
      auto new_values = CombineTwoTuple(left_tuple_, *left_schema_, right_tuple, plan_->InnerTableSchema(), false);
      *tuple = Tuple(new_values, &plan_->OutputSchema());
      return true;
    }
  }
}

auto NestIndexJoinExecutor::CombineTwoTuple(const Tuple &ltuple, const Schema &lschema, const Tuple &rtuple,
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
