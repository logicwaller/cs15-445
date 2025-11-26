//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>

#include "execution/executors/update_executor.h"

namespace bustub {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      child_schema_(plan_->GetChildPlan()->OutputSchema()) {
  // As of Fall 2022, you DON'T need to implement update executor to have perfect score in project 3 / project 4.
}

void UpdateExecutor::Init() {
  // 初始化child_exec
  child_executor_->Init();
  // 获取table的metadata
  auto catalog = exec_ctx_->GetCatalog();
  table_info_ = catalog->GetTable(plan_->table_oid_).get();
  // 记录是否已插入过
  have_updated_ = false;

  indexes_ = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);
}

auto UpdateExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  // 记录插入的行数
  int32_t update_rows = 0;
  while (true) {
    // 获取child_exec的下一个tuple
    Tuple child_tuple{};
    const bool status = child_executor_->Next(&child_tuple, rid);

    if (!status) {           // 若child_exec没有next
      if (!have_updated_) {  // 若本次executor更新过数据，则返回true
        // 返回插入的行数
        *tuple = Tuple(std::vector<Value>{Value(TypeId::INTEGER, update_rows)}, &GetOutputSchema());
        have_updated_ = true;
        return true;
      }
      return false;
    }

    auto tuple_meta = table_info_->table_->GetTupleMeta(*rid);

    // 进行删除
    tuple_meta.is_deleted_ = true;
    table_info_->table_->UpdateTupleMeta(tuple_meta, *rid);  // 在table_heap里删除
    tuple_meta.is_deleted_ = false;                          // 将is_deleted_设为false便于下面插入使用
    // 对每个index进行删除
    for (const auto &index : indexes_) {
      for (const auto &col_idx : index->index_->GetKeyAttrs()) {
        Value key = child_tuple.GetValue(&plan_->GetChildPlan()->OutputSchema(), col_idx);
        Schema key_schema(std::vector<Column>{key.GetColumn()});
        index->index_->DeleteEntry(Tuple(std::vector<Value>{key}, &key_schema), *rid, exec_ctx_->GetTransaction());
      }
    }

    // 进行插入
    std::vector<Value> values{};
    values.reserve(GetOutputSchema().GetColumnCount());
    for (auto expr : plan_->target_expressions_) {
      values.push_back(expr->Evaluate(&child_tuple, child_schema_));
    }
    Tuple new_tuple = Tuple{values, &child_schema_};
    auto insert_rid = table_info_->table_->InsertTuple(tuple_meta, new_tuple, exec_ctx_->GetLockManager(),
                                                       exec_ctx_->GetTransaction(), plan_->GetTableOid());
    if (!insert_rid.has_value()) {
      BUSTUB_ENSURE(insert_rid.has_value(), "Failed to insert tuple, tuple is too large");
      return false;
    }
    // 对每个index进行插入
    for (const auto &index : indexes_) {
      for (const auto &col_idx : index->index_->GetKeyAttrs()) {
        Value key = new_tuple.GetValue(&plan_->GetChildPlan()->OutputSchema(), col_idx);
        Schema key_schema(std::vector<Column>{key.GetColumn()});
        index->index_->InsertEntry(Tuple(std::vector<Value>{key}, &key_schema), insert_rid.value(),
                                   exec_ctx_->GetTransaction());
      }
    }

    update_rows++;
  }
}

}  // namespace bustub
