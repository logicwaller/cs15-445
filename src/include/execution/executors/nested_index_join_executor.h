//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.h
//
// Identification: src/include/execution/executors/nested_index_join_executor.h
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/expressions/abstract_expression.h"
#include "execution/plans/nested_index_join_plan.h"
#include "storage/table/tmp_tuple.h"
#include "storage/table/tuple.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * IndexJoinExecutor executes index join operations.
 */
class NestIndexJoinExecutor : public AbstractExecutor {
 public:
  /**
   * Creates a new nested index join executor.
   * @param exec_ctx the context that the nested index join should be performed in
   * @param plan the nested index join plan to be executed
   * @param child_executor the outer table
   */
  NestIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                        std::unique_ptr<AbstractExecutor> &&child_executor);

  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

  void Init() override;

  auto Next(Tuple *tuple, RID *rid) -> bool override;

 private:
  /** 将左右tuple合并为一个tuple；若is_right_null为真则使right_tuple为null_value */
  auto CombineTwoTuple(const Tuple &ltuple, const Schema &lschema, const Tuple &rtuple, const Schema &rschema,
                       bool is_right_null) -> std::vector<Value>;

  /** The nested index join plan node. */
  const NestedIndexJoinPlanNode *plan_;

  std::unique_ptr<AbstractExecutor> child_executor_;

  // 记录catalog相关信息
  std::shared_ptr<IndexInfo> index_info_;
  std::shared_ptr<TableInfo> table_info_;

  // 记录left_tuple相关信息
  Tuple left_tuple_;
  SchemaRef left_schema_;

  // 记录在右child匹配到的tuple的rid
  std::vector<RID> match_res_;

  // 记录最终join后的schema
  SchemaRef join_schema_;
};
}  // namespace bustub
