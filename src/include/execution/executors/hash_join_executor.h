//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.h
//
// Identification: src/include/execution/executors/hash_join_executor.h
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <utility>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/aggregation_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "storage/table/tuple.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * HashJoinExecutor executes a nested-loop JOIN on two tables.
 */
class HashJoinExecutor : public AbstractExecutor {
 public:
  /**
   * Construct a new HashJoinExecutor instance.
   * @param exec_ctx The executor context
   * @param plan The HashJoin join plan to be executed
   * @param left_child The child executor that produces tuples for the left side of join
   * @param right_child The child executor that produces tuples for the right side of join
   */
  HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                   std::unique_ptr<AbstractExecutor> &&left_child, std::unique_ptr<AbstractExecutor> &&right_child);

  /** Initialize the join */
  void Init() override;

  /**
   * Yield the next tuple from the join.
   * @param[out] tuple The next tuple produced by the join.
   * @param[out] rid The next tuple RID, not used by hash join.
   * @return `true` if a tuple was produced, `false` if there are no more tuples.
   */
  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the join */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); };

 private:
  /** 获取给定tuple的join_key，is_left表示是否为left_child */
  auto MakeGroupByKey(const Tuple *tuple, bool is_left) -> AggregateKey;

  /** 获取一个tuple的所有value；若is_null为真则返回相应的null_value */
  auto GetAllValueFromTuple(const Tuple &tuple, const Schema &schema, bool is_null) const -> std::vector<Value>;

  /** The HashJoin plan node to be executed. */
  const HashJoinPlanNode *plan_;

  std::unique_ptr<AbstractExecutor> left_child_;
  std::unique_ptr<AbstractExecutor> right_child_;
  SchemaRef left_schema_;
  SchemaRef right_schema_;

  // 记录left_tuple相关状态
  Tuple left_tuple_;
  // 记录匹配到当前left_tuple的right_tuple
  std::vector<Tuple> match_res_;
  // 记录join后的schema
  SchemaRef join_schema_;

  // 记录right_child中以join_key为键，所构建的哈希表(这里复用aggregateKey用于hash)
  std::unordered_map<AggregateKey, std::vector<Tuple>> hash_map_;
};

}  // namespace bustub
