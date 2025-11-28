#include "optimizer/optimizer.h"

#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/index_scan_plan.h"
#include "execution/plans/seq_scan_plan.h"

namespace bustub {

auto Optimizer::OptimizeSeqScanAsIndexScan(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(student): implement seq scan with predicate -> index scan optimizer rule
  // The Filter Predicate Pushdown has been enabled for you in optimizer.cpp when forcing starter rule
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.push_back(OptimizeSeqScanAsIndexScan(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() == PlanType::SeqScan) {
    const auto &seq_scan_plan = dynamic_cast<const SeqScanPlanNode &>(*optimized_plan);
    const auto &indexes = catalog_.GetTableIndexes(seq_scan_plan.table_name_);
    if (seq_scan_plan.filter_predicate_ != nullptr) {  // 当seq_scan的filter存在时，检验是否可以优化为index_scan
      bool can_be_optimized = true;                    // 记录是否能被优化
      std::optional<uint32_t> col_idx;                 // 记录找到的列的下标，只能有一个
      std::vector<Value> find_value;                   // 记录找到的常量,保证不重复
      index_oid_t index_oid;                           // 记录找到索引的index_oid
      std::vector<AbstractExpressionRef> pred_keys;    // 记录被优化后在index_scan中的pred_key,若为空则说明不能被优化

      // 递归遍历filter的所有children，直到找到comparison_expression，判断是否能优化
      std::vector<AbstractExpressionRef> seq_children{seq_scan_plan.filter_predicate_};
      while (!seq_children.empty()) {
        std::vector<AbstractExpressionRef> tem_children;  // 记录新的children，即下一轮遍历的children

        for (const auto &seq_child : seq_children) {  // 遍历seq_children
          const auto &logic = std::dynamic_pointer_cast<LogicExpression>(seq_child);
          const auto &compare = std::dynamic_pointer_cast<ComparisonExpression>(seq_child);
          if (logic) {  // 若expression是logic，继续递归其children
            for (const auto &tem_child : seq_child->GetChildren()) {
              tem_children.push_back(tem_child);
            }
          } else if (compare) {  // 若是compare，则筛选其中=的部分，检查是否有作为索引的列
            const auto &lchild = seq_child->GetChildAt(0);
            const auto &rchild = seq_child->GetChildAt(1);
            if (compare->comp_type_ == ComparisonType::Equal) {
              auto lcolum = std::dynamic_pointer_cast<ColumnValueExpression>(lchild);
              auto rcolum = std::dynamic_pointer_cast<ColumnValueExpression>(rchild);
              auto rvalue = std::dynamic_pointer_cast<ConstantValueExpression>(rchild);  // 记录查找到的常量
              if (lcolum != nullptr && rcolum != nullptr) {  // 若比较的左右child都为column，则不能被优化
                can_be_optimized = false;
                break;
              }

              if (lcolum == nullptr &&
                  rcolum != nullptr) {  // 若右为column 左为值，则交换二者，即保证左一定为column 右一定为值
                lcolum = std::move(rcolum);
                rvalue = std::dynamic_pointer_cast<ConstantValueExpression>(lchild);
              }

              // 检验是否只有一个列
              uint32_t col = lcolum->GetColIdx();
              if (!col_idx.has_value() || col_idx == col) {
                col_idx = col;
              } else {  // 若出现多个index_oid，则无法优化
                can_be_optimized = false;
                break;
              }

              // 检验lcolum是否在索引中
              for (const auto &index : indexes) {  // 遍历所有索引，判断col是否在其中
                const auto &keyAttrs = index->index_->GetKeyAttrs();
                auto finded = std::find(keyAttrs.begin(), keyAttrs.end(), col);
                if (finded != keyAttrs.end()) {  // 若列对应一个index
                  index_oid = index->index_oid_;

                  // 查找find_value，看是否有与当前rvalue重复的值

                  bool has_duplated = false;
                  for (auto v : find_value) {
                    if (v.CompareEquals(rvalue->val_) == CmpBool::CmpTrue) {
                      has_duplated = true;
                      break;
                    }
                  }
                  if (!has_duplated) {  // 只有不重复才插入pred_keys
                    find_value.push_back(rvalue->val_);
                    pred_keys.push_back(rvalue);  // 此时的seq_child即为一个pred_keys
                  }
                  break;
                }
              }
            }
          }
        }

        if (!can_be_optimized) {  // 若确定无法优化，则直接break
          break;
        }

        seq_children = std::move(tem_children);  // 遍历新children
      }

      // 若能被优化，则返回相应IndexScanPlanNode
      if (can_be_optimized && !pred_keys.empty()) {
        return std::make_shared<IndexScanPlanNode>(seq_scan_plan.output_schema_, seq_scan_plan.table_oid_, index_oid,
                                                   seq_scan_plan.filter_predicate_, pred_keys);
      }
    }
  }

  return optimized_plan;
}

}  // namespace bustub
