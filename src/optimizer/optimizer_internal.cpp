#include <algorithm>
#include <memory>
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

void OptimizerHelperFunction() {}

auto Optimizer::CombineComparsionExpression(const std::vector<AbstractExpressionRef> &comparsions,
                                            std::optional<size_t> left_column_cnt,
                                            std::optional<size_t> right_column_cnt) -> AbstractExpressionRef {
  if (comparsions.empty()) {  //若为空则返回nullptr
    return nullptr;
  }

  AbstractExpressionRef res;
  if (comparsions.size() == 1) {  // 若一共只有一个compare，则返回该compare
    res = comparsions.back();
  } else {
    // 将comparsion用and组合起来
    res = std::make_shared<LogicExpression>(comparsions[0], comparsions[1], LogicType::And);  //记录组合结果
    size_t s = comparsions.size();
    for (size_t i = 2; i < s; i++) {
      res->children_[1] = std::make_shared<LogicExpression>(comparsions[i], res->children_[1], LogicType::And);
    }
  }

  if (!left_column_cnt.has_value()) {
    BUSTUB_ENSURE(!left_column_cnt.has_value() && !right_column_cnt.has_value(),
                  "CombineComparsion Error: left and right should both have no value");
    return res;
  }
  return Optimizer::RewriteExpressionForJoin(res, left_column_cnt.value(), right_column_cnt.value());
}

auto Optimizer::OptimizeMergeFilterMultiJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // 对于嵌套nlj，将外层nlj的部分filter下移，使其尽可能进行hashjoin
  // 具体就是在(#a.b = #c.d)中，若(a==1||c==1)，则将其保留在本层，否则下移
  // 对于(column compare value)中，只保留column为#1.x的部分
  // 本优化应从上往下将filter下移，故先对本plan优化再对其child优化

  auto optimized_plan = plan;
  // 只有当前plan及其childAt(1)都为nlj才能被优化
  if (optimized_plan->GetType() == PlanType::NestedLoopJoin && !optimized_plan->children_.empty() &&
      optimized_plan->GetChildAt(0)->GetType() == PlanType::NestedLoopJoin) {
    auto nlj_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan);
    auto &nlj_plan_lchild = dynamic_cast<const NestedLoopJoinPlanNode &>(*nlj_plan.GetLeftPlan());

    bool can_be_optimized = true;  //记录是否能被优化

    std::vector<AbstractExpressionRef> preds{nlj_plan.Predicate()};
    // 递归解析predicate_，只保留和本plan有关的部分，其余处理后下移
    std::vector<AbstractExpressionRef> now_comparsions;    //记录留在本层的preds
    std::vector<AbstractExpressionRef> child_comparsions;  //记录插入child的preds
    while (!preds.empty()) {
      std::vector<AbstractExpressionRef> next_preds;  //记录下一次递归的preds

      for (const auto &pred : preds) {
        const auto &compare = std::dynamic_pointer_cast<ComparisonExpression>(pred);
        if (compare) {
          bool insert_child = true;  //记录本compare插入child还是保留在本层
          const auto &lchild = pred->GetChildAt(0);
          const auto &rchild = pred->GetChildAt(1);
          auto lcolumn = std::dynamic_pointer_cast<ColumnValueExpression>(lchild);
          auto rcolumn = std::dynamic_pointer_cast<ColumnValueExpression>(rchild);

          if (lcolumn && rcolumn) {  //若左右都是column
            // 若都是#0.x，则插入child
            insert_child = lcolumn->GetTupleIdx() == 0 && rcolumn->GetTupleIdx() == 0;
          } else {  // 否则直接插入本层即可
            const auto &rvalue = std::dynamic_pointer_cast<ConstantValueExpression>(rchild);
            if (!rvalue) {  //若rvalue无值，则说明right_child为column，使其赋值到lcolumn，即保证lcolumn一定有值
              lcolumn = rcolumn;
            }
            BUSTUB_ENSURE(lcolumn, "opt error: lcolumn has no value");

            // 若column对左表操作，则将其插入child
            insert_child = (lcolumn->GetTupleIdx() == 0);
          }

          // 插入对应comparsions
          if (insert_child) {
            child_comparsions.push_back(compare);
          } else {
            now_comparsions.push_back(compare);
          }
        } else {  //若不是compare则只会是logic
          const auto &logic = std::dynamic_pointer_cast<LogicExpression>(pred);
          if (logic->logic_type_ == LogicType::Or) {  //若logic判断出现or，则不能被优化
            can_be_optimized = false;
            break;
          }
          // 遍历其child
          for (const auto &child : logic->GetChildren()) {
            next_preds.push_back(child);
          }
        }
      }

      if (!can_be_optimized) {
        break;
      }

      // 遍历下一层preds
      preds = next_preds;
    }

    // 若能被优化，则将optimized_plan设为优化后结果
    if (can_be_optimized) {
      // 设置新的child_plan
      auto child_lchild = nlj_plan_lchild.GetLeftPlan();
      auto child_rchild = nlj_plan_lchild.GetRightPlan();
      auto child_plan = NestedLoopJoinPlanNode(
          nlj_plan_lchild.output_schema_, child_lchild, child_rchild,
          CombineComparsionExpression(child_comparsions, child_lchild->OutputSchema().GetColumnCount(),
                                      child_rchild->OutputSchema().GetColumnCount()),
          nlj_plan_lchild.join_type_);

      // 设置optimized_plan
      optimized_plan = std::make_shared<NestedLoopJoinPlanNode>(
          nlj_plan.output_schema_, std::make_shared<NestedLoopJoinPlanNode>(child_plan), nlj_plan.GetRightPlan(),
          CombineComparsionExpression(now_comparsions), nlj_plan.join_type_);
    }
  }

  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : optimized_plan->GetChildren()) {
    children.emplace_back(OptimizeMergeFilterMultiJoin(child));
  }
  optimized_plan = optimized_plan->CloneWithChildren(std::move(children));

  return optimized_plan;
}

}  // namespace bustub
