//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.cpp
//
// Identification: src/execution/delete_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "execution/executors/delete_executor.h"

namespace bustub {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void DeleteExecutor::Init() {
  // 初始化child_executor_
  child_executor_->Init();
  // 初始化have_deleted_
  have_deleted_ = false;

  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->GetTableOid()).get();
  indexes_ = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);
}

auto DeleteExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  // 记录删除的行数
  int32_t delete_rows = 0;
  while (true) {
    // 获取child_exec的下一个tuple
    Tuple child_tuple{};
    const bool status = child_executor_->Next(&child_tuple, rid);

    if (!status) {           // 若child_exec没有next
      if (!have_deleted_) {  // 若本次executor更新过数据，则返回true
        *tuple = Tuple(std::vector<Value>{Value(TypeId::INTEGER, delete_rows)}, &GetOutputSchema());
        have_deleted_ = true;
        return true;
      }
      return false;
    }

    auto tuple_meta = table_info_->table_->GetTupleMeta(*rid);

    // 进行删除
    tuple_meta.is_deleted_ = true;
    table_info_->table_->UpdateTupleMeta(tuple_meta, *rid);
    // 在index里删除相应记录
    for (const auto &index : indexes_) {
      for (const auto &col_idx : index->index_->GetKeyAttrs()) {
        Value key = child_tuple.GetValue(&plan_->GetChildPlan()->OutputSchema(), col_idx);
        index->index_->DeleteEntry(Tuple(std::vector<Value>{key}, &index->key_schema_), *rid,
                                   exec_ctx_->GetTransaction());
      }
    }

    delete_rows++;
  }
}

}  // namespace bustub
