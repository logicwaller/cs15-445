#include <algorithm>
#include <memory>
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

auto Optimizer::OptimizeNLJAsHashJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(student): implement NestedLoopJoin -> HashJoin optimizer rule
  // Note for 2023 Fall: You should support join keys of any number of conjunction of equi-conditions:
  // E.g. <column expr> = <column expr> AND <column expr> = <column expr> AND ...

  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeNLJAsHashJoin(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() == PlanType::NestedLoopJoin) {
    const auto &nlj_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan);
    // 确保nlj只有两个child
    BUSTUB_ENSURE(nlj_plan.children_.size() == 2, "NLJ should have exactly 2 children.");

    bool can_be_optimized = true;                               // 记录是否能被优化
    std::vector<AbstractExpressionRef> left_key_expr;           // 记录left_key的表达式
    std::vector<AbstractExpressionRef> right_key_expr;          // 记录right_key的表达式
    std::vector<AbstractExpressionRef> left_filter_node_expr;   // 记录新建的左filter_node内的表达式
    std::vector<AbstractExpressionRef> right_filter_node_expr;  // 记录新建的右filter_node内的表达式

    // 递归遍历predicate，判断是否能被优化
    std::vector<AbstractExpressionRef> preds{nlj_plan.Predicate()};
    while (!preds.empty()) {
      std::vector<AbstractExpressionRef> next_preds;  // 记录下一次递归的preds

      for (const auto &pred : preds) {  // 遍历当前preds
        const auto &compare = std::dynamic_pointer_cast<ComparisonExpression>(pred);
        if (compare) {  // 若是compare，则判断是否能被优化
          const auto &lchild = pred->GetChildAt(0);
          const auto &rchild = pred->GetChildAt(1);
          auto lcolumn = std::dynamic_pointer_cast<ColumnValueExpression>(lchild);
          auto rcolumn = std::dynamic_pointer_cast<ColumnValueExpression>(rchild);

          if (lcolumn && rcolumn) {  //若是列和列的比较
            // 若列和列的判断出现非=的判断，或左右的tupleIdx一致，则说明不能被优化
            if ((compare->comp_type_ != ComparisonType::Equal) || (lcolumn->GetTupleIdx() == rcolumn->GetTupleIdx())) {
              can_be_optimized = false;
              break;
            }
            // 若lcolumn存的是right side of join，则交换二者，即保证lcolumn存left side, rcolumn存right side
            if (lcolumn->GetTupleIdx() == 1) {
              auto tem = lcolumn;
              lcolumn = rcolumn;
              rcolumn = tem;
            }
            // 存入对应expr
            left_key_expr.push_back(lcolumn);
            right_key_expr.push_back(
                std::make_shared<ColumnValueExpression>(0, rcolumn->GetColIdx(), rcolumn->GetReturnType()));
          } else {  // 若比较的左右child有一个不是column，则将表达式放入filter_node中
            const auto &rvalue = std::dynamic_pointer_cast<ConstantValueExpression>(rchild);
            bool is_left_column = true;  //记录left_child是否为column
            if (!rvalue) {  //若rvalue无值，则说明right_child为column，使其赋值到lcolumn，即保证lcolumn一定有值
              is_left_column = false;
              lcolumn = rcolumn;
            }
            BUSTUB_ENSURE(lcolumn, "opt error: lcolumn has no value");
            if (lcolumn->GetTupleIdx() == 0) {
              left_filter_node_expr.push_back(compare);
            } else {
              // 对于tuplexIdx = 1的column，需要将其tupleIdx改为0后再进行filter
              if (is_left_column) {  //若原来是left_child为column
                ColumnValueExpression new_lchild(0, lcolumn->GetColIdx(), lcolumn->GetReturnType());
                right_filter_node_expr.push_back(std::make_shared<ComparisonExpression>(
                    std::make_shared<ColumnValueExpression>(new_lchild), rchild, compare->comp_type_));
              } else {  //若原来是rithg_child为column
                ColumnValueExpression new_rchild(0, rcolumn->GetColIdx(), rcolumn->GetReturnType());
                right_filter_node_expr.push_back(std::make_shared<ComparisonExpression>(
                    lchild, std::make_shared<ColumnValueExpression>(new_rchild), compare->comp_type_));
              }
            }
          }
        } else {  // 若不是comparison，则继续递归其children
          for (const auto &child : pred->GetChildren()) {
            next_preds.push_back(child);
          }
        }
      }

      if (!can_be_optimized) {  // 若不能被优化，则break
        break;
      }

      // 遍历下一层的preds
      preds = next_preds;
    }

    // 若可以被优化且left_key_expr不为空(此时right_key也一定不为空)，则进行优化
    if (can_be_optimized && !left_key_expr.empty()) {
      AbstractPlanNodeRef left_child = nlj_plan.GetLeftPlan();
      AbstractPlanNodeRef right_child = nlj_plan.GetRightPlan();
      if (!left_filter_node_expr.empty()) {
        left_child = std::make_shared<FilterPlanNode>(left_child->output_schema_,
                                                      CombineComparsionExpression(left_filter_node_expr), left_child);
      }
      if (!right_filter_node_expr.empty()) {
        right_child = std::make_shared<FilterPlanNode>(
            right_child->output_schema_, CombineComparsionExpression(right_filter_node_expr), right_child);
      }

      return std::make_shared<HashJoinPlanNode>(nlj_plan.output_schema_, left_child, right_child, left_key_expr,
                                                right_key_expr, nlj_plan.GetJoinType());
    }
  }

  return optimized_plan;
}

}  // namespace bustub
