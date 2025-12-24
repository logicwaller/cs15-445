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
  auto schema_size = schema->GetColumnCount();  // 记录tuple中的value个数
  // moded_values初始化为base_tuple的value
  std::vector<Value> moded_values = GetAllValueFromTuple(base_tuple, schema);

  // 判断是否返回null
  if (undo_logs.empty()) {        //若无undo_log
    if (base_meta.is_deleted_) {  //若无undo_log且已被删除，则返回null
      return std::nullopt;
    }
    //若无undo_log且未被删除，则返回原base_tuple
    return base_tuple;
  }
  if (undo_logs.back().is_deleted_) {  //若undo_log最后一项为delete则返回null
    return std::nullopt;
  }

  // 遍历undo_logs，进行相应修改
  for (const auto &undo_log : undo_logs) {
    // 获取被更改的schema
    const auto &mod_schema = GetUndoLogSchema(undo_log, schema);

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
  if (base_tuple == nullptr) {  // 若是插入
    return UndoLog{true, std::vector<bool>(), Tuple(), ts, prev_version};
  }
  if (target_tuple == nullptr) {  //若是删除
    return UndoLog{false, std::vector<bool>(schema->GetColumnCount(), true), *base_tuple, ts, prev_version};
  }
  // 获取base_tuple和target_tuple的modified_fields和相应的tuple
  const auto &modified_pair = GenerateDiffBetweenTuples(schema, base_tuple, target_tuple);
  return UndoLog{false, modified_pair.first, modified_pair.second, ts, prev_version};
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
  // 对于is_deleted_的log则直接返回该log即可
  if (log.is_deleted_) {
    return log;
  }

  std::pair<std::vector<bool>, Tuple> modified_pair;
  const auto &column_size = schema->GetColumnCount();
  //若base_tuple为nullptr,则说明上次进行删除，log内存储的即为原始tuple；否则正常使用base_tuple即可
  const Tuple *origin_tuple = (base_tuple == nullptr ? &log.tuple_ : base_tuple);
  if (target_tuple == nullptr) {  //若本次进行删除，则视为更改了所有value
    modified_pair.first = std::vector<bool>(column_size, true);
    modified_pair.second = *origin_tuple;
  } else {
    modified_pair = GenerateDiffBetweenTuples(schema, origin_tuple, target_tuple);
  }

  // 进行增量更新log
  Schema log_schema = GetUndoLogSchema(log, schema);
  std::vector<bool> modified_fileds(column_size, false);
  std::vector<Column> log_columns = log_schema.GetColumns();
  std::vector<Value> log_values = GetAllValueFromTuple(log.tuple_, &log_schema);
  uint32_t lg_index = 0;  //记录遍历到第几个log_values
  for (uint32_t i = 0; i < column_size; i++) {
    modified_fileds[i] = log.modified_fields_[i] || modified_pair.first[i];
    if (log.modified_fields_[i]) {
      lg_index++;
    } else if (modified_pair.first[i]) {
      log_values.insert(log_values.begin() + lg_index, origin_tuple->GetValue(schema, i));
      log_columns.insert(log_columns.begin() + lg_index, schema->GetColumn(i));
      lg_index++;
    }
  }

  return UndoLog{false, modified_fileds, Tuple(log_values, std::make_shared<Schema>(log_columns).get()), log.ts_,
                 log.prev_version_};
}

void TxnMgrDbg(const std::string &info, TransactionManager *txn_mgr, const TableInfo *table_info,
               TableHeap *table_heap) {
  // always use stderr for printing logs...
  fmt::println(stderr, "debug_hook: {}", info);

  auto tuple_it = table_heap->MakeIterator();
  while (!tuple_it.IsEnd()) {
    const auto &rid = tuple_it.GetRID();
    const auto &ts = table_heap->GetTupleMeta(rid).ts_;
    const auto &is_delete = table_heap->GetTupleMeta(rid).is_deleted_;
    fmt::print(stderr, "RID={}/{} ts={} {}", rid.GetPageId(), rid.GetSlotNum(),
               ts >= TXN_START_ID ? "txn" + std::to_string(ts ^ TXN_START_ID) : std::to_string(ts),
               is_delete ? "<del marker> " : "");

    const auto &base_tuple = tuple_it.GetTuple();
    const auto &values = GetAllValueFromTuple(tuple_it.GetTuple().second, &table_info->schema_);
    fmt::print(stderr, "tuple=(");
    for (uint32_t i = 0; i < values.size(); i++) {
      fmt::print(stderr, values[i].IsNull() ? "<NULL>" : values[i].ToString());
      if (i < values.size() - 1) {
        fmt::print(stderr, ", ");
      }
    }
    fmt::println(stderr, ")");

    auto undo_link = txn_mgr->GetUndoLink(rid);
    if (undo_link.has_value()) {
      auto undo_log = txn_mgr->GetUndoLogOptional(undo_link.value());
      std::vector<UndoLog> undo_logs;
      while (undo_log.has_value()) {
        undo_logs.emplace_back(undo_log.value());
        fmt::print(stderr, "  txn{}@{} ", undo_link->prev_txn_ ^ TXN_START_ID, undo_link->prev_log_idx_);
        if (undo_log->is_deleted_) {
          fmt::print(stderr, "<del> ");
        } else {
          const auto &tem_tuple =
              ReconstructTuple(&table_info->schema_, base_tuple.second, base_tuple.first, undo_logs);
          const auto &tem_values = GetAllValueFromTuple(tem_tuple.value(), &table_info->schema_);
          fmt::print(stderr, "(");
          for (uint32_t i = 0; i < tem_values.size(); i++) {
            fmt::print(stderr, tem_values[i].IsNull() ? "_" : tem_values[i].ToString());
            if (i < tem_values.size() - 1) {
              fmt::print(stderr, ", ");
            }
          }
          fmt::print(stderr, ") ");
        }
        fmt::println(stderr, "ts={}", undo_log->ts_);
        undo_link = undo_log->prev_version_;
        undo_log = txn_mgr->GetUndoLogOptional(undo_link.value());
      }
    }

    ++tuple_it;
  }
  // We recommend implementing this function as traversing the table heap and print the version chain. An example
  // output of our reference solution:
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

/**
 * @brief 返回给定tuple的所有value
 */
auto GetAllValueFromTuple(const Tuple &tuple, const Schema *schema) -> std::vector<Value> {
  auto schema_size = schema->GetColumnCount();  // 记录tuple中的value个数
  std::vector<Value> res_values(schema_size);   // 记录更改后的values
  for (uint32_t i = 0; i < schema_size; i++) {
    res_values[i] = tuple.GetValue(schema, i);
  }
  return res_values;
}

/**
 * @brief 生成从base_tuple到target_tuple修改的modified_fileds和tuple
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update
 * @param target_tuple The target tuple after the update
 * @return 返回由modified_fileds和更改前的tuple构成的pair
 */
auto GenerateDiffBetweenTuples(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple)
    -> std::pair<std::vector<bool>, Tuple> {
  const auto &column_size = schema->GetColumnCount();
  // 返回被获取被更改的values和对应的schema
  const auto &base_values = GetAllValueFromTuple(*base_tuple, schema);
  const auto &target_values = GetAllValueFromTuple(*target_tuple, schema);
  std::vector<bool> modified_fileds(column_size, false);
  std::vector<Value> modified_values;
  std::vector<Column> modified_columns;
  for (uint32_t i = 0; i < column_size; i++) {
    if (!base_values[i].CompareExactlyEquals(target_values[i])) {
      modified_fileds[i] = true;
      modified_values.emplace_back(base_values[i]);
      modified_columns.emplace_back(schema->GetColumn(i));
    }
  }
  return {modified_fileds, Tuple(modified_values, std::make_shared<Schema>(Schema(modified_columns)).get())};
}

/**
 * @brief 返回给定log的tuple的schema
 */
auto GetUndoLogSchema(const UndoLog &log, const Schema *base_schema) -> Schema {
  const auto &column_size = base_schema->GetColumnCount();
  std::vector<Column> columns;
  for (uint32_t i = 0; i < column_size; i++) {
    if (log.modified_fields_[i]) {
      columns.emplace_back(base_schema->GetColumn(i));
    }
  }
  return Schema(columns);
}

/**
 * @brief 判断txn是否发生写-写冲突
 *
 * @param is_delete 若为true则是删除操作；否则为更新操作
 */
auto IsWriteWriteConflict(const RID &rid, const TableInfo *table_info, const Transaction *txn, bool is_delete) -> bool {
  const auto &tuple_meta = table_info->table_->GetTupleMeta(rid);

  if (tuple_meta.ts_ != txn->GetTransactionTempTs()) {
    // 若(case1)正删除一个已被其他txn删除的tuple 或(case2)tuple正被别的未提交的txn修改
    // 或(case3)tuple已被提交且提交时间比txn的read_ts更新，则视为出现冲突
    if ((is_delete && tuple_meta.is_deleted_) || tuple_meta.ts_ >= TXN_START_ID || tuple_meta.ts_ > txn->GetReadTs()) {
      return true;
    }
  }

  return false;
}

/**
 * @brief 对于给定schema，生成全是null的tuple
 */
auto GenerateNullTupleForSchema(const Schema *schema) -> Tuple {
  std::vector<Value> res_value;
  const auto &columns = schema->GetColumns();
  res_value.reserve(columns.size());
  for (const auto &column : columns) {
    res_value.emplace_back(ValueFactory::GetNullValueByType(column.GetType()));
  }
  return {Tuple(res_value, schema)};
}
}  // namespace bustub
