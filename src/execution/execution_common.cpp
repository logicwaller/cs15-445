//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// execution_common.cpp
//
// Identification: src/execution/execution_common.cpp
//
// Copyright (c) 2024-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/execution_common.h"

#include "catalog/catalog.h"
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "fmt/core.h"
#include "storage/table/table_heap.h"

namespace bustub {

TupleComparator::TupleComparator(std::vector<OrderBy> order_bys) : order_bys_(std::move(order_bys)) {}

auto TupleComparator::operator()(const SortEntry &entry_a, const SortEntry &entry_b) const -> bool {
  int s = entry_a.first.size();
  for (int i = 0; i < s; i++) {
    Value a = entry_a.first[i];
    Value b = entry_b.first[i];
    if (a.CompareEquals(b) == CmpBool::CmpTrue) {  // 若相等，则比较下一个order_by
      continue;
    }

    switch (order_bys_[i].first) {
      case OrderByType::DEFAULT:  // DEFAULT和ASC行为一致
      case OrderByType::ASC:
        return a.CompareLessThan(b) == CmpBool::CmpTrue;
      case OrderByType::DESC:
        return a.CompareGreaterThan(b) == CmpBool::CmpTrue;
      case OrderByType::INVALID:
        BUSTUB_ENSURE(false, "invalid compare type");
    }
  }
  return false;
}

auto GenerateSortKey(const Tuple &tuple, const std::vector<OrderBy> &order_bys, const Schema &schema) -> SortKey {
  SortKey res;
  for (const auto &order_by : order_bys) {
    res.push_back(order_by.second->Evaluate(&tuple, schema));
  }
  return res;
}

/**
 * Above are all you need for P3.
 * You can ignore the remaining part of this file until P4.
 */

/**
 * @brief Reconstruct a tuple by applying the provided undo logs from the base tuple. All logs in the undo_logs are
 * applied regardless of the timestamp
 *
 * @param schema The schema of the base tuple and the returned tuple.
 * @param base_tuple The base tuple to start the reconstruction from.
 * @param base_meta The metadata of the base tuple.
 * @param undo_logs The list of undo logs to apply during the reconstruction, the front is applied first.
 * @return An optional tuple that represents the reconstructed tuple. If the tuple is deleted as the result, returns
 * std::nullopt.
 */
auto ReconstructTuple(const Schema *schema, const Tuple &base_tuple, const TupleMeta &base_meta,
                      const std::vector<UndoLog> &undo_logs) -> std::optional<Tuple> {
  auto schema_size = schema->GetColumnCount();   // 记录tuple中的value个数
  std::vector<Value> moded_values(schema_size);  // 记录更改后的values
  // moded_values初始化为base_tuple的value
  for (uint32_t i = 0; i < schema_size; i++) {
    moded_values[i] = base_tuple.GetValue(schema, i);
  }

  // 判断是否返回null
  if (undo_logs.empty()) {        //若无undo_log
    if (base_meta.is_deleted_) {  //若无undo_log且已被删除，则返回null
      return std::nullopt;
    } else {  //若无undo_log且未被删除，则返回原base_tuple
      return base_tuple;
    }
  } else if (undo_logs.back().is_deleted_) {  //若undo_log最后一项为delete则返回null
    return std::nullopt;
  }

  // 遍历undo_logs，进行相应修改
  for (const auto &undo_log : undo_logs) {
    // 获取被更改的schema
    std::vector<Column> mod_columns;
    for (uint32_t i = 0; i < schema_size; i++) {
      if (undo_log.modified_fields_[i]) {
        mod_columns.emplace_back(schema->GetColumn(i));
      }
    }
    Schema mod_schema(mod_columns);

    // 解析log中的tuple_并将更改应用于res_tuple
    uint32_t mod_index = 0;  //记录更改到第几个mod_schema
    for (uint32_t i = 0; i < schema_size; i++) {
      if (undo_log.modified_fields_[i]) {  //在被修改的部分插入被修改后的value
        moded_values[i] = undo_log.tuple_.GetValue(&mod_schema, mod_index++);
      }
    }
  }

  // 返回修改后的values构成的tuple
  Tuple res_tuple(moded_values, schema);
  res_tuple.SetRid(base_tuple.GetRid());
  return res_tuple;
}

/**
 * @brief Collects the undo logs sufficient to reconstruct the tuple w.r.t. the txn.
 *
 * @param rid The RID of the tuple.
 * @param base_meta The metadata of the base tuple.
 * @param base_tuple The base tuple.
 * @param undo_link The undo link to the latest undo log.
 * @param txn The transaction.
 * @param txn_mgr The transaction manager.
 * @return An optional vector of undo logs to pass to ReconstructTuple(). std::nullopt if the tuple did not exist at the
 * time.
 */
auto CollectUndoLogs(RID rid, const TupleMeta &base_meta, const Tuple &base_tuple, std::optional<UndoLink> undo_link,
                     Transaction *txn, TransactionManager *txn_mgr) -> std::optional<std::vector<UndoLog>> {
  //若为提交的txn就是本txn 或 tuple已被提交且最新版本小于read_ts，则返回空log
  if (base_meta.ts_ <= txn->GetReadTs() || base_meta.ts_ == txn->GetTransactionTempTs()) {
    return std::vector<UndoLog>();
  }

  // 若undo_link不存在，则视为不存在该tuple
  if (!undo_link.has_value()) {
    return std::nullopt;
  }

  // 遍历undo_link，获取对应时间的undolog
  std::vector<UndoLog> res_logs;          //记录最终返回的longs
  UndoLink tem_link = undo_link.value();  // 记录当前遍历到的link
  while (true) {
    // 获取对应log
    auto tem_log = txn_mgr->GetUndoLogOptional(tem_link);
    // 若遍历log结束后仍未找到小于read_ts的log，则视为tuple在read_ts不存在
    if (!tem_log.has_value()) {
      return std::nullopt;
    }
    res_logs.emplace_back(tem_log.value());
    // 若tem_log的ts小于read_ts，则说明已获取正确的版本，进行break;
    if (tem_log->ts_ <= txn->GetReadTs()) {
      break;
    }
    tem_link = tem_log->prev_version_;
  }

  return res_logs;
}

/**
 * @brief Generates a new undo log as the transaction tries to modify this tuple at the first time.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param ts The timestamp of the base tuple.
 * @param prev_version The undo link to the latest undo log of this tuple.
 * @return The generated undo log.
 */
auto GenerateNewUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple, timestamp_t ts,
                        UndoLink prev_version) -> UndoLog {
  UNIMPLEMENTED("not implemented");
}

/**
 * @brief Generate the updated undo log to replace the old one, whereas the tuple is already modified by this txn once.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param log The original undo log.
 * @return The updated undo log.
 */
auto GenerateUpdatedUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple,
                            const UndoLog &log) -> UndoLog {
  UNIMPLEMENTED("not implemented");
}

void TxnMgrDbg(const std::string &info, TransactionManager *txn_mgr, const TableInfo *table_info,
               TableHeap *table_heap) {
  // always use stderr for printing logs...
  fmt::println(stderr, "debug_hook: {}", info);

  fmt::println(
      stderr,
      "You see this line of text because you have not implemented `TxnMgrDbg`. You should do this once you have "
      "finished task 2. Implementing this helper function will save you a lot of time for debugging in later tasks.");

  // We recommend implementing this function as traversing the table heap and print the version chain. An example output
  // of our reference solution:
  //
  // debug_hook: before verify scan
  // RID=0/0 ts=txn8 tuple=(1, <NULL>, <NULL>)
  //   txn8@0 (2, _, _) ts=1
  // RID=0/1 ts=3 tuple=(3, <NULL>, <NULL>)
  //   txn5@0 <del> ts=2
  //   txn3@0 (4, <NULL>, <NULL>) ts=1
  // RID=0/2 ts=4 <del marker> tuple=(<NULL>, <NULL>, <NULL>)
  //   txn7@0 (5, <NULL>, <NULL>) ts=3
  // RID=0/3 ts=txn6 <del marker> tuple=(<NULL>, <NULL>, <NULL>)
  //   txn6@0 (6, <NULL>, <NULL>) ts=2
  //   txn3@1 (7, _, _) ts=1
}

}  // namespace bustub
