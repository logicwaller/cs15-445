//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/executors/index_scan_executor.h"

namespace bustub {
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_);
  index_info_ = exec_ctx_->GetCatalog()->GetIndex(plan_->index_oid_);
  tree_ = dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get());
  pred_keys_index_ = 0;
  if (plan_->pred_keys_.empty()) {  // 若pred_key不存在，则说明是ordered scan,需要初始化iterator
    iterator_.emplace(tree_->GetBeginIterator());
  }
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (true) {
    // 获取当前遍历到的tuple的rid
    std::vector<RID> result_rid;
    if (iterator_.has_value()) {  // 若iterator被初始化，则说明是ordered scan
      // 若iterator_为end，则直接返回false
      if (iterator_.value().IsEnd()) {
        return false;
      }

      auto tem = *iterator_.value();
      ++iterator_.value();
      result_rid.push_back(tem.second);
    } else {  // 否则是 point lookup
      // 若遍历完pred_keys，则直接返回false
      if (pred_keys_index_ >= plan_->pred_keys_.size()) {
        return false;
      }

      // 获取要查找的key;获取左右兄弟中的constant_value_expression
      const auto &const_value =
          std::dynamic_pointer_cast<ConstantValueExpression>(plan_->pred_keys_[pred_keys_index_++]);

      Schema tem_schema(std::vector<Column>{const_value->val_.GetColumn()});
      // 在index内获取key所在的rid
      tree_->ScanKey(Tuple(std::vector<Value>{const_value->val_}, &tem_schema), &result_rid,
                     exec_ctx_->GetTransaction());
    }

    // 遍历查找到的rid(由于本数据库不支持重复索引，所以result_rid其实只会有一个值)
    for (const auto &res_rid : result_rid) {
      auto pair = table_info_->table_->GetTuple(res_rid);

      /** proj4-对获取的tuple进行处理，返回txn中的read_ts时可见的tuple */
      GenerateTupleVisibleToLog(exec_ctx_->GetTransaction(), exec_ctx_->GetTransactionManager(), &pair,
                                &GetOutputSchema());

      if (!pair.first.is_deleted_) {
        *tuple = pair.second;
        *rid = res_rid;

        if (plan_->filter_predicate_ != nullptr) {  // 若filter存在，则检验
          /** proj4-在txn存储filter */
          exec_ctx_->GetTransaction()->AppendScanPredicate(table_info_->oid_, plan_->filter_predicate_);

          Value value = plan_->filter_predicate_->Evaluate(tuple, GetOutputSchema());
          if (value.CompareEquals(Value(TypeId::BOOLEAN, 1)) == CmpBool::CmpTrue) {
            // 若能通过fliter则返回true
            return true;
          }
        } else {  // 若filter不存在，则直接返回true
          return true;
        }
      }
    }
  }
}

}  // namespace bustub
