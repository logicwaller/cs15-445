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
  const auto &txn = exec_ctx_->GetTransaction();
  const auto &txn_mgr = exec_ctx_->GetTransactionManager();
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

    /** proj4-检查index内是否存在该tuple；修改tableHeap内插入的tuple的tupleMeta；修改txn内的writeset */
    InsertOrUpdateDelTuple(child_tuple, plan_->GetTableOid(), indexes_, table_info_.get(), exec_ctx_->GetLockManager(),
                           txn, txn_mgr);
    insert_rows++;
  }
}
}  // namespace bustub
