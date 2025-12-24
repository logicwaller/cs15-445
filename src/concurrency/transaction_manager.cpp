//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// transaction_manager.cpp
//
// Identification: src/concurrency/transaction_manager.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/transaction_manager.h"

#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include "catalog/catalog.h"
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "concurrency/transaction.h"
#include "execution/execution_common.h"
#include "storage/table/table_heap.h"
#include "storage/table/tuple.h"
#include "type/type_id.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

auto TransactionManager::Begin(IsolationLevel isolation_level) -> Transaction * {
  std::unique_lock<std::shared_mutex> l(txn_map_mutex_);
  auto txn_id = next_txn_id_++;
  auto txn = std::make_unique<Transaction>(txn_id, isolation_level);
  auto *txn_ref = txn.get();
  txn_map_.insert(std::make_pair(txn_id, std::move(txn)));

  // TODO(fall2023): set the timestamps here. Watermark updated below.
  txn_ref->read_ts_ = last_commit_ts_.load();

  running_txns_.AddTxn(txn_ref->read_ts_);
  return txn_ref;
}

auto TransactionManager::VerifyTxn(Transaction *txn) -> bool { return true; }

auto TransactionManager::Commit(Transaction *txn) -> bool {
  std::unique_lock<std::mutex> commit_lck(commit_mutex_);

  // TODO(fall2023): acquire commit ts!
  auto commit_ts = last_commit_ts_.load() + 1;  //获取commit的ts，待更新所有tuple后再执行last_commit_ts++

  if (txn->state_ != TransactionState::RUNNING) {
    throw Exception("txn not in running state");
  }

  if (txn->GetIsolationLevel() == IsolationLevel::SERIALIZABLE) {
    if (!VerifyTxn(txn)) {
      commit_lck.unlock();
      Abort(txn);
      return false;
    }
  }

  // TODO(fall2023): Implement the commit logic!
  // 更新所有txn更改过的tuple的ts
  for (const auto &[table_id, rids] : txn->GetWriteSets()) {
    auto &table_heap = catalog_->GetTable(table_id)->table_;
    for (const auto &rid : rids) {
      // 更新tuple_meta
      bool is_deleted = table_heap->GetTupleMeta(rid).is_deleted_;
      if (is_deleted && !GetUndoLink(rid).has_value()) {  //若该tuple是被同一txn插入后又删除，则将ts设为0
        table_heap->UpdateTupleMeta(TupleMeta{0, is_deleted}, rid);
      } else {  // 否则正常插入
        table_heap->UpdateTupleMeta(TupleMeta{commit_ts, is_deleted}, rid);
      }
    }
  }

  std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);

  // TODO(fall2023): set commit timestamp + update last committed timestamp here.
  last_commit_ts_.fetch_add(1);
  txn->commit_ts_ = last_commit_ts_.load();

  txn->state_ = TransactionState::COMMITTED;
  running_txns_.UpdateCommitTs(txn->commit_ts_);
  running_txns_.RemoveTxn(txn->read_ts_);

  return true;
}

void TransactionManager::Abort(Transaction *txn) {
  if (txn->state_ != TransactionState::RUNNING && txn->state_ != TransactionState::TAINTED) {
    throw Exception("txn not in running / tainted state");
  }

  // TODO(fall2023): Implement the abort logic!

  std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);
  txn->state_ = TransactionState::ABORTED;
  running_txns_.RemoveTxn(txn->read_ts_);
}

void TransactionManager::GarbageCollection() {
  std::shared_lock<std::shared_mutex> map_lck(txn_map_mutex_);
  std::vector<txn_id_t> delete_txns;  //记录将被删除的txn_id
  for (const auto &[txn_id, txn] : txn_map_) {
    if (txn->state_ == TransactionState::RUNNING || txn->state_ == TransactionState::TAINTED) {  //运行中的txn不会被删除
      continue;
    }

    bool can_be_delete = true;  //记录txn能否被清除

    // 从txn所更改的tuple的视角，判断该txn储存的log中是否存在某个log是其对应tuple中相较于watermark的最新版本的log
    const auto &write_set = txn->GetWriteSets();
    for (const auto &[table_oid, rids] : write_set) {
      const auto &table_info = catalog_->GetTable(table_oid);
      for (const auto &rid : rids) {
        const auto &tuple_ts = table_info->table_->GetTupleMeta(rid).ts_;
        //只有watermark小于table_heap里存储的tuple_ts时，最新的log才有可能对watermark对应的时间可见
        if (running_txns_.GetWatermark() < tuple_ts) {
          const auto &undo_link = GetUndoLink(rid);
          // 若txn存储了最新版本的log，则不能被删
          if (undo_link.has_value() && undo_link->prev_txn_ == txn_id) {
            can_be_delete = false;
            break;
          }
        }
      }
      if (!can_be_delete) {
        break;
      }
    }
    if (!can_be_delete) {
      continue;
    }

    // 从txn所存储的log视角，判断txn储存的log是否都ts小于watermark
    const auto &log_size = txn->GetUndoLogNum();
    for (size_t i = 0; i < log_size; i++) {
      const auto &log = txn->GetUndoLog(i);
      // 该log的ts大于watermark，则该txn不能被删
      if (log.ts_ >= running_txns_.GetWatermark()) {
        can_be_delete = false;
        break;
      }
    }
    // 若可以删除则添加该txn_id
    if (can_be_delete) {
      delete_txns.emplace_back(txn_id);
    }
  }

  for (const auto &delete_txn : delete_txns) {
    txn_map_.erase(delete_txn);
  }
}

}  // namespace bustub
