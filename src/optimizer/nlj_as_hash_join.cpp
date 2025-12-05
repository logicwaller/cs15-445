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

    bool can_be_optimized = true;                       // 记录是否能被优化
    std::vector<AbstractExpressionRef> left_key_expr;   // 记录left_key的表达式
    std::vector<AbstractExpressionRef> right_key_expr;  // 记录right_key的表达式

    // 递归遍历predicate，判断是否能被优化
    std::vector<AbstractExpressionRef> preds{nlj_plan.Predicate()};
    while (!preds.empty()) {
      std::vector<AbstractExpressionRef> next_preds;  // 记录下一次递归的preds

      for (const auto &pred : preds) {  // 遍历当前preds
        const auto &compare = std::dynamic_pointer_cast<ComparisonExpression>(pred);
        if (compare) {  // 若是compare，则判断是否能被优化
          // 若出现非=的判断，则说明不能被优化
          if (compare->comp_type_ != ComparisonType::Equal) {
            can_be_optimized = false;
            break;
          }

          auto lcolumn = std::dynamic_pointer_cast<ColumnValueExpression>(pred->GetChildAt(0));
          auto rcolumn = std::dynamic_pointer_cast<ColumnValueExpression>(pred->GetChildAt(1));

          if (lcolumn == nullptr || rcolumn == nullptr) {  // 若比较的左右child有一个不是column，则不能被优化
            can_be_optimized = false;
            break;
          }

          if (lcolumn->GetTupleIdx() == 1) {  // 若lcolumn存的是right side of join，则交换二者
            auto tem = lcolumn;
            lcolumn = rcolumn;
            rcolumn = tem;
          }

          // 存入对应expr
          left_key_expr.push_back(lcolumn);
          right_key_expr.push_back(rcolumn);
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

    if (can_be_optimized) {
      return std::make_shared<HashJoinPlanNode>(nlj_plan.output_schema_, nlj_plan.GetLeftPlan(),
                                                nlj_plan.GetRightPlan(), left_key_expr, right_key_expr,
                                                nlj_plan.GetJoinType());
    }
  }

  return optimized_plan;
}

}  // namespace bustub
