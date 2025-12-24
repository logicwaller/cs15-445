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
    const auto &tuple_meta = table_info_->table_->GetTupleMeta(old_tuple.GetRid());

    // 获取更新后的tuple
    std::vector<Value> values{};
    values.reserve(GetOutputSchema().GetColumnCount());
    for (auto &expr : plan_->target_expressions_) {
      values.push_back(expr->Evaluate(&old_tuple, child_schema_));
    }
    Tuple new_tuple = Tuple{values, &child_schema_};

    // 更新undo_log和tuple
    const auto &last_undo_link = txn_mgr->GetUndoLink(tuple_rid);  //获取该tuple的上个link
    if (!last_undo_link.has_value() && tuple_meta.ts_ == txn->GetTransactionTempTs()) {
      // 若没有last_link且该tuple是本次txn进行修改,则说明该tuple为本次txn插入，无需修改undo_log
      table_info_->table_->UpdateTupleInPlace(TupleMeta{txn->GetTransactionTempTs(), false}, new_tuple, tuple_rid);
    } else {
      //若last_undo_link有值，则正常更新
      if (tuple_meta.ts_ != txn->GetTransactionTempTs()) {  // 若是第一次更新，则产生new_log
        const auto &undo_log =
            GenerateNewUndoLog(&child_schema_, &old_tuple, &new_tuple, tuple_meta.ts_,
                               last_undo_link.has_value() ? last_undo_link.value() : UndoLink{INVALID_TXN_ID, 0});
        txn_mgr->UpdateUndoLink(tuple_rid, txn->AppendUndoLog(undo_log));
      } else {  //否则进行update_log
        UndoLog undo_log;
        undo_log =
            GenerateUpdatedUndoLog(&child_schema_, &old_tuple, &new_tuple, txn_mgr->GetUndoLog(last_undo_link.value()));
        txn->ModifyUndoLog(txn_mgr->GetUndoLink(tuple_rid)->prev_log_idx_, undo_log);
      }
      const auto &undo_link = txn_mgr->GetUndoLink(tuple_rid);

      // 进行更新
      if (!UpdateTupleAndUndoLink(txn_mgr, tuple_rid, undo_link, table_info_->table_.get(), txn,
                                  TupleMeta{txn->GetTransactionTempTs(), false}, new_tuple)) {
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

      // 对每个index进行插入
      Tuple new_key = new_tuple.KeyFromTuple(table_info_->schema_, index->key_schema_, index->index_->GetKeyAttrs());
      index->index_->InsertEntry(new_key, tuple_rid, exec_ctx_->GetTransaction());
    }
  }

  if (!update_tuples_.empty()) {  // 若本次executor更新过数据，则返回true
    // 返回插入的行数
    *tuple = Tuple(std::vector<Value>{Value(TypeId::INTEGER, (int)update_tuples_.size())}, &GetOutputSchema());
    have_updated_ = true;
    return true;
  }
  // 若未更新过数据，则返回false
  return false;

  /** proj4-以下无用 */
  // 记录插入的行数
  // int32_t update_rows = 0;
  // while (true) {
  //   // 获取child_exec的下一个tuple
  //   Tuple child_tuple{};
  //   const bool status = child_executor_->Next(&child_tuple, rid);

  //   if (!status) {           // 若child_exec没有next
  //     if (!have_updated_) {  // 若本次executor更新过数据，则返回true
  //       // 返回插入的行数
  //       *tuple = Tuple(std::vector<Value>{Value(TypeId::INTEGER, update_rows)}, &GetOutputSchema());
  //       have_updated_ = true;
  //       return true;
  //     }
  //     return false;
  //   }

  //   auto tuple_meta = table_info_->table_->GetTupleMeta(*rid);

  //   // 进行删除
  //   tuple_meta.is_deleted_ = true;
  //   table_info_->table_->UpdateTupleMeta(tuple_meta, *rid);  // 在table_heap里删除
  //   tuple_meta.is_deleted_ = false;                          // 将is_deleted_设为false便于下面插入使用
  //   // 对每个index进行删除
  //   for (const auto &index : indexes_) {
  //     Tuple key = child_tuple.KeyFromTuple(table_info_->schema_, index->key_schema_, index->index_->GetKeyAttrs());
  //     index->index_->DeleteEntry(key, *rid, exec_ctx_->GetTransaction());
  //   }

  //   // 进行插入
  //   std::vector<Value> values{};
  //   values.reserve(GetOutputSchema().GetColumnCount());
  //   for (auto &expr : plan_->target_expressions_) {
  //     values.push_back(expr->Evaluate(&child_tuple, child_schema_));
  //   }
  //   Tuple new_tuple = Tuple{values, &child_schema_};
  //   auto insert_rid = table_info_->table_->InsertTuple(tuple_meta, new_tuple, exec_ctx_->GetLockManager(),
  //                                                      exec_ctx_->GetTransaction(), plan_->GetTableOid());
  //   if (!insert_rid.has_value()) {
  //     BUSTUB_ENSURE(insert_rid.has_value(), "Failed to insert tuple, tuple is too large");
  //     return false;
  //   }
  //   // 对每个index进行插入
  //   for (const auto &index : indexes_) {
  //     Tuple key = new_tuple.KeyFromTuple(table_info_->schema_, index->key_schema_, index->index_->GetKeyAttrs());
  //     index->index_->InsertEntry(key, insert_rid.value(), exec_ctx_->GetTransaction());
  //   }

  //   update_rows++;
  // }
}

}  // namespace bustub
