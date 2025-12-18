#include "concurrency/watermark.h"
#include <algorithm>
#include <exception>
#include "common/exception.h"

namespace bustub {

auto Watermark::AddTxn(timestamp_t read_ts) -> void {
  if (read_ts < commit_ts_) {
    throw Exception("read ts < commit ts");
  }

  // TODO(fall2023): implement me!
  watermark_ = watermark_ < read_ts ? watermark_ : read_ts;  // 若read_ts较小则wartermark取该值
  auto it = current_reads_.find(read_ts);
  if (it != current_reads_.end()) {
    it->second++;
  } else {
    current_reads_.insert({read_ts, 1});
    exist_reads_.push(read_ts);
  }
}

auto Watermark::RemoveTxn(timestamp_t read_ts) -> void {
  // TODO(fall2023): implement me!
  auto it = current_reads_.find(read_ts);
  if (--it->second == 0) {  //若已没有改read_ts的txn，则在map中去除该记录
    // 去除current_reads_里的数据
    current_reads_.erase(it);

    if (watermark_ == read_ts) {     //若去除的记录为wartermark，则寻找下一个map中最小的timestamp
      if (current_reads_.empty()) {  //若记录已空，则设置watermark为正无穷
        watermark_ = LONG_MAX;
      }

      // 更新并获取最小的exist_reads
      while (!exist_reads_.empty()) {
        auto tem = exist_reads_.top();
        if (current_reads_.find(tem) != current_reads_.end()) {
          // 若exist_reads_.top()在current_reads_里存在，则更新watermark_
          watermark_ = tem;
          break;
        } else {
          // 否则pop，继续判断下一个top是否存在
          exist_reads_.pop();
        }
      }
    }
  }
}

}  // namespace bustub
