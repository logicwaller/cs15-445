#include <set>
#include "execution/expressions/column_value_expression.h"
#include "execution/plans/aggregation_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

/**
 * @note You may use this function to implement column pruning optimization.
 */
auto Optimizer::OptimizeColumnPruning(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // Your code here
  // 自顶向下，遇到投影则考虑是否对child剪枝，目前只实现child为投影或聚合的情况

  auto optimized_plan = plan;
  if (optimized_plan->GetType() == PlanType::Projection) {
    const auto &proj_plan = dynamic_cast<const ProjectionPlanNode &>(*optimized_plan);

    if (proj_plan.GetChildPlan()->GetType() == PlanType::Projection ||
        proj_plan.GetChildPlan()->GetType() == PlanType::Aggregation) {
      /* 解析expressions，提取所有的col_idx */
      std::vector<uint32_t> col_idxs;  //按序记录proj_plan的exprs中的col_idx
      for (const auto &expr : proj_plan.GetExpressions()) {
        // 递归遍历expr，直至获取column
        std::vector<AbstractExpressionRef> preds{expr};
        while (!preds.empty()) {
          std::vector<AbstractExpressionRef> next_preds;  //记录下一次递归的preds

          for (const auto &pred : preds) {
            const auto &column = std::dynamic_pointer_cast<ColumnValueExpression>(pred);
            if (column) {
              BUSTUB_ENSURE(column->GetTupleIdx() == 0, "projection's column's tuple idx is not 0")
              auto col_idx = column->GetColIdx();
              //按序插入col_idx
              auto it = std::lower_bound(col_idxs.begin(), col_idxs.end(), col_idx);
              if (it == col_idxs.end() || *it != col_idx) {
                col_idxs.insert(it, col_idx);
              }
            } else {  //否则遍历其children
              for (const auto &child_pred : pred->GetChildren()) {
                next_preds.push_back(child_pred);
              }
            }
          }

          preds = next_preds;
        }
      }

      //必须保证col_idx是按序从0到s-1排列的
      auto s = col_idxs.size();
      for (uint32_t i = 0; i < s; i++) {
        BUSTUB_ENSURE(col_idxs[i] == i, "unimplement: skip some number in projection' col_idx");
      }

      /* 对child为投影或聚合分别处理 */
      AbstractPlanNodeRef res_child_plan;
      if (proj_plan.GetChildPlan()->GetType() == PlanType::Projection) {
        const auto &child_plan = dynamic_cast<const ProjectionPlanNode &>(*proj_plan.GetChildPlan());
        std::vector<AbstractExpressionRef> new_exprs;
        std::vector<Column> new_columns;
        new_exprs.reserve(col_idxs.size());
        new_columns.reserve(col_idxs.size());
        for (auto idx : col_idxs) {
          new_exprs.emplace_back(child_plan.expressions_[idx]);
          new_columns.emplace_back(child_plan.expressions_[idx]->GetReturnType());
        }
        res_child_plan = std::make_shared<ProjectionPlanNode>(std::make_shared<Schema>(new_columns), new_exprs,
                                                              child_plan.GetChildPlan());
      } else {  //对聚合处理
        const auto &child_plan = dynamic_cast<const AggregationPlanNode &>(*proj_plan.GetChildPlan());
        std::vector<AbstractExpressionRef> new_aggergates;
        std::vector<AggregationType> new_agg_types;
        std::vector<Column> new_columns;
        auto group_size = child_plan.group_bys_.size();
        new_aggergates.reserve(col_idxs.size() - group_size);
        new_agg_types.reserve(col_idxs.size() - group_size);
        new_columns.reserve(col_idxs.size());
        // 聚合的前若干位位是gruop by的列,需要额外处理
        for (const auto &group : child_plan.group_bys_) {
          new_columns.emplace_back(group->GetReturnType());
        }
        for (auto idx : col_idxs) {
          if (idx >= group_size) {
            new_aggergates.emplace_back(child_plan.aggregates_[idx - group_size]);
            new_agg_types.emplace_back(child_plan.agg_types_[idx - group_size]);
            new_columns.emplace_back(child_plan.aggregates_[idx - group_size]->GetReturnType());
          }
        }
        res_child_plan =
            std::make_shared<AggregationPlanNode>(std::make_shared<Schema>(new_columns), child_plan.GetChildPlan(),
                                                  child_plan.group_bys_, new_aggergates, new_agg_types);
      }

      // 设置optimized_plan
      optimized_plan =
          std::make_shared<ProjectionPlanNode>(proj_plan.output_schema_, proj_plan.expressions_, res_child_plan);
    }
  }

  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : optimized_plan->GetChildren()) {
    children.emplace_back(OptimizeColumnPruning(child));
  }
  optimized_plan = optimized_plan->CloneWithChildren(std::move(children));

  return optimized_plan;
}

}  // namespace bustub
