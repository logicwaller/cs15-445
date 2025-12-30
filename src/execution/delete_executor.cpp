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
  // 初始化have_deleted
  have_deleted_ = false;

  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->GetTableOid()).get();
  indexes_ = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);

  /** proj4-先获取所有待删除的tuple，进行check后再全部进行更新 */
  RID rid;  //传入next的值，无意义
  while (true) {
    Tuple child_tuple{};
    const bool status = child_executor_->Next(&child_tuple, &rid);
    if (!status) {
      break;
    }
    delete_tuples_rid_.emplace_back(child_tuple.GetRid());
  }
}

auto DeleteExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  /** proj4-对所有的delete_tuples_进行删除 */
  if (have_deleted_) {  //若进行过删除则直接返回false
    return false;
  }

  const auto &txn = exec_ctx_->GetTransaction();
  const auto &txn_mgr = exec_ctx_->GetTransactionManager();
  for (const auto &tuple_rid : delete_tuples_rid_) {
    const auto &[tuple_meta, old_tuple, undo_link] = GetTupleAndUndoLink(txn_mgr, table_info_->table_.get(), tuple_rid);

    // 判断是否出现写-写冲突
    if (IsWriteWriteConflict(tuple_rid, tuple_meta, exec_ctx_->GetTransaction(), true)) {
      // 若出现写-写冲突，则abort txn并将txn设置为tainted，最终throw ExecutionException
      exec_ctx_->GetTransaction()->SetTainted();
      throw ExecutionException("write-write conflict");
    }

    // 更新undo_log和tuple_meta
    GenerateLogAndUpdateTuple(&old_tuple, nullptr, tuple_meta, undo_link, table_info_, txn, txn_mgr);

    // proj4要求不在index内进行deleteEntry，只需要将index对应tuple设为删除状态即可
  }

  if (!delete_tuples_rid_.empty()) {  // 若本次executor删除过数据，则返回true
    // 返回插入的行数
    *tuple = Tuple(std::vector<Value>{Value(TypeId::INTEGER, static_cast<int>(delete_tuples_rid_.size()))},
                   &GetOutputSchema());
    have_deleted_ = true;
    return true;
  }
  // 若未删除过数据，则返回false
  return false;
}

}  // namespace bustub
