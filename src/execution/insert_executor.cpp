//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "execution/executors/insert_executor.h"

namespace bustub {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() {
  // child_executor可能未执行init，需要在该节点执行子节点的init
  child_executor_->Init();
  // 记录是否已插入过
  have_inserted_ = false;

  auto catalog = exec_ctx_->GetCatalog();
  table_info_ = catalog->GetTable(plan_->table_oid_);
  indexes_ = catalog->GetTableIndexes(table_info_->name_);
}

auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  // 记录插入的行数
  int32_t insert_rows = 0;
  while (true) {
    // 获取child_exec的下一个tuple
    Tuple child_tuple{};
    const bool status = child_executor_->Next(&child_tuple, rid);

    if (!status) {
      if (!have_inserted_) {  // 若本次executor插入过数据，则返回true
        // 返回插入的行数
        *tuple = Tuple(std::vector<Value>{Value(TypeId::INTEGER, insert_rows)}, &GetOutputSchema());
        have_inserted_ = true;
        return true;
      }
      // 否则返回false
      return false;
    }

    // 修改tableHeap，插入child_tuple;meta传入一个新建的空tupleMeta即可
    auto insert_rid = table_info_->table_->InsertTuple(TupleMeta(), child_tuple, exec_ctx_->GetLockManager(),
                                                       exec_ctx_->GetTransaction());

    /** proj4-修改tableHeap内插入的tuple的tupleMeta；修改txn内的writeset */
    // TODO:怎么添加check函数
    table_info_->table_->UpdateTupleMeta(TupleMeta{exec_ctx_->GetTransaction()->GetTransactionTempTs(), false},
                                         insert_rid.value());
    exec_ctx_->GetTransaction()->AppendWriteSet(plan_->GetTableOid(), insert_rid.value());

    // 对每个index都插入相关数据
    for (const auto &index : indexes_) {
      Tuple key = child_tuple.KeyFromTuple(table_info_->schema_, index->key_schema_, index->index_->GetKeyAttrs());
      index->index_->InsertEntry(key, insert_rid.value(), exec_ctx_->GetTransaction());
    }
    insert_rows++;
  }
}
}  // namespace bustub
