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
    // 判断是否出现写-写冲突
    if (IsWriteWriteConflict(rid, table_info_, exec_ctx_->GetTransaction(), true)) {
      // 若出现写-写冲突，则abort txn并将txn设置为tainted，最终throw ExecutionException
      // exec_ctx_->GetTransactionManager()->Abort(exec_ctx_->GetTransaction());
      exec_ctx_->GetTransaction()->SetTainted();
      throw ExecutionException("write-write conflict");
    }
    delete_tuples_.emplace_back(child_tuple);
  }
}

auto DeleteExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  /** proj4-对所有的delete_tuples_进行删除 */
  if (have_deleted_) {  //若进行过删除则直接返回false
    return false;
  }

  const auto &txn = exec_ctx_->GetTransaction();
  const auto &txn_mgr = exec_ctx_->GetTransactionManager();
  const auto &child_schema = plan_->GetChildPlan()->OutputSchema();
  const auto &null_tuple = GenerateNullTupleForSchema(&child_schema);
  for (const auto &old_tuple : delete_tuples_) {
    const auto &tuple_rid = old_tuple.GetRid();
    const auto &tuple_meta = table_info_->table_->GetTupleMeta(old_tuple.GetRid());

    // 更新undo_log和tuple_meta
    const auto &last_undo_link = txn_mgr->GetUndoLink(tuple_rid);  //获取该tuple的上个link
    if (!last_undo_link.has_value() && tuple_meta.ts_ == txn->GetTransactionTempTs()) {
      // 若没有last_link且该tuple是本次txn进行修改,则说明该tuple为本次txn插入，无需修改undo_log
      table_info_->table_->UpdateTupleMeta(TupleMeta{txn->GetTransactionTempTs(), true}, tuple_rid);
    } else {                                                                        //否则正常更新
      if (tuple_meta.ts_ != exec_ctx_->GetTransaction()->GetTransactionTempTs()) {  // 若是第一次更新，则产生new_log
        const auto &undo_log =
            GenerateNewUndoLog(&child_schema, &old_tuple, nullptr, tuple_meta.ts_,
                               last_undo_link.has_value() ? last_undo_link.value() : UndoLink{INVALID_TXN_ID, 0});
        txn_mgr->UpdateUndoLink(tuple_rid, txn->AppendUndoLog(undo_log));
      } else {  //否则进行update_log
        UndoLog undo_log;
        undo_log =
            GenerateUpdatedUndoLog(&child_schema, &old_tuple, nullptr, txn_mgr->GetUndoLog(last_undo_link.value()));
        txn->ModifyUndoLog(txn_mgr->GetUndoLink(tuple_rid)->prev_log_idx_, undo_log);
      }
      const auto &undo_link = txn_mgr->GetUndoLink(tuple_rid);

      // 更新tuple和undolog TODO:传入什么check函数
      if (!UpdateTupleAndUndoLink(txn_mgr, tuple_rid, undo_link, table_info_->table_.get(), txn,
                                  TupleMeta{txn->GetTransactionTempTs(), true}, null_tuple)) {
        throw ExecutionException("UpdateInplace error");
      }
    }

    // 向txn的writeSet添加记录
    txn->AppendWriteSet(plan_->GetTableOid(), tuple_rid);

    // 更新index
    for (const auto &index : indexes_) {
      // 对每个index进行删除
      Tuple old_key = old_tuple.KeyFromTuple(table_info_->schema_, index->key_schema_, index->index_->GetKeyAttrs());
      index->index_->DeleteEntry(old_key, tuple_rid, exec_ctx_->GetTransaction());
    }
  }

  if (!delete_tuples_.empty()) {  // 若本次executor删除过数据，则返回true
    // 返回插入的行数
    *tuple =
        Tuple(std::vector<Value>{Value(TypeId::INTEGER, static_cast<int>(delete_tuples_.size()))}, &GetOutputSchema());
    have_deleted_ = true;
    return true;
  }
  // 若未删除过数据，则返回false
  return false;

  /** proj4-以下无用 */
  // // 记录删除的行数
  // int32_t delete_rows = 0;
  // while (true) {
  //   // 获取child_exec的下一个tuple
  //   Tuple child_tuple{};
  //   const bool status = child_executor_->Next(&child_tuple, rid);

  //   if (!status) {           // 若child_exec没有next
  //     if (!have_deleted_) {  // 若本次executor更新过数据，则返回true
  //       *tuple = Tuple(std::vector<Value>{Value(TypeId::INTEGER, delete_rows)}, &GetOutputSchema());
  //       have_deleted_ = true;
  //       return true;
  //     }
  //     return false;
  //   }

  //   auto tuple_meta = table_info_->table_->GetTupleMeta(*rid);

  //   // 进行删除
  //   tuple_meta.is_deleted_ = true;
  //   table_info_->table_->UpdateTupleMeta(tuple_meta, *rid);
  //   // 在index里删除相应记录
  //   for (const auto &index : indexes_) {
  //     Tuple key = child_tuple.KeyFromTuple(table_info_->schema_, index->key_schema_, index->index_->GetKeyAttrs());
  //     index->index_->DeleteEntry(key, *rid, exec_ctx_->GetTransaction());
  //   }

  //   delete_rows++;
  // }
}

}  // namespace bustub
