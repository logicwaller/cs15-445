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
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {
  // As of Fall 2022, you DON'T need to implement update executor to have perfect score in project 3 / project 4.
}

void UpdateExecutor::Init() {
  // 初始化child_exec
  child_executor_->Init();
  // 获取table的metadata
  auto catalog = exec_ctx_->GetCatalog();
  table_info_ = catalog->GetTable(plan_->table_oid_).get();

  have_updated_ = false;

  indexes_ = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);

  /** proj4-先获取所有待更新的tuple，进行check后再全部进行更新 */
  RID rid;  //传入next的值，无意义
  while (true) {
    Tuple child_tuple{};
    const bool status = child_executor_->Next(&child_tuple, &rid);
    if (!status) {
      break;
    }
    // 判断是否出现写-写冲突
    if (IsWriteWriteConflict(rid, table_info_, exec_ctx_->GetTransaction(), false)) {
      // 若出现写-写冲突，则abort txn并将txn设置为tainted，最终throw ExecutionException
      // exec_ctx_->GetTransactionManager()->Abort(exec_ctx_->GetTransaction());
      exec_ctx_->GetTransaction()->SetTainted();
      throw ExecutionException("write-write conflict");
    }
    update_tuples_.emplace_back(child_tuple);
  }
}

auto UpdateExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  /** proj4-对所有的update_tuples_进行更新 */
  if (have_updated_) {  //若执行过更新则返回false
    return false;
  }

  const auto &txn = exec_ctx_->GetTransaction();
  const auto &txn_mgr = exec_ctx_->GetTransactionManager();
  for (const auto &old_tuple : update_tuples_) {
    const auto &tuple_rid = old_tuple.GetRid();

    // 获取更新后的tuple
    std::vector<Value> values{};
    values.reserve(GetOutputSchema().GetColumnCount());
    for (auto &expr : plan_->target_expressions_) {
      values.push_back(expr->Evaluate(&old_tuple, table_info_->schema_));
    }
    Tuple new_tuple = Tuple{values, &table_info_->schema_};

    // 更新undo_log和tuple
    GenerateLogAndUpdateTuple(&old_tuple, &new_tuple, table_info_, txn, txn_mgr);

    // 向txn的writeSet添加记录
    txn->AppendWriteSet(plan_->GetTableOid(), tuple_rid);

    // 更新index
    for (const auto &index : indexes_) {
      // 对每个index进行删除
      Tuple old_key = old_tuple.KeyFromTuple(table_info_->schema_, index->key_schema_, index->index_->GetKeyAttrs());
      index->index_->DeleteEntry(old_key, tuple_rid, exec_ctx_->GetTransaction());

      // 对每个index进行插入
      Tuple new_key = new_tuple.KeyFromTuple(table_info_->schema_, index->key_schema_, index->index_->GetKeyAttrs());
      index->index_->InsertEntry(new_key, tuple_rid, exec_ctx_->GetTransaction());
    }
  }

  if (!update_tuples_.empty()) {  // 若本次executor更新过数据，则返回true
    // 返回插入的行数
    *tuple =
        Tuple(std::vector<Value>{Value(TypeId::INTEGER, static_cast<int>(update_tuples_.size()))}, &GetOutputSchema());
    have_updated_ = true;
    return true;
  }
  // 若未更新过数据，则返回false
  return false;
}

}  // namespace bustub
