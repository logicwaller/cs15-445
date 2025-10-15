//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"
#include "common/exception.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict() -> std::optional<frame_id_t> {
  size_t max_time = 0;
  size_t inf_max_time = 0;  // 记录多个inf情况下最近的最近访问记录
  std::optional<frame_id_t> max_fram = std::nullopt;
  for (std::pair pair : node_store_) {
    if (pair.second.is_evictable_) {
      size_t ktime = GetNodeKTime(pair.second);

      if (ktime == UINT64_MAX) {  // 若ktime为inf,返回最远的最近访问frame
        max_time = UINT64_MAX;    // 将max_time设置为inf,防止进入普通情况的判断
        size_t relative_time = current_timestamp_ - pair.second.history_.back();
        if (relative_time > inf_max_time) {
          inf_max_time = relative_time;
          max_fram.emplace(pair.first);
        }
      } else if (ktime > max_time) {
        max_time = ktime;
        max_fram.emplace(pair.first);
      }
    }
  }
  // 若驱逐frame,则删除对应记录
  if (max_fram != std::nullopt) {
    node_store_.erase(max_fram.value());
    curr_size_--;
  }
  return max_fram;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  auto find_frame = node_store_.find(frame_id);
  if (find_frame != node_store_.end()) {  // node_store_存在frame_id,则插入记录
    LRUKNode &find_node = find_frame->second;
    find_node.history_.push_back(current_timestamp_++);
  } else {
    if (node_store_.size() == replacer_size_) {  // 若超出replacer_size,报错
      BUSTUB_ASSERT(1, "RecordAccess Error: Invalid frame id(size larger than replacer_size)");
    } else {
      std::list<size_t> history;
      history.push_back(current_timestamp_++);
      LRUKNode node(history, frame_id, false);  // 默认不可驱逐
      node_store_.insert(std::pair<frame_id_t, LRUKNode>(frame_id, node));
    }
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  auto find_frame = node_store_.find(frame_id);
  if (find_frame != node_store_.end()) {  // 若frame_id存在记录
    LRUKNode &find_node = find_frame->second;
    if (find_node.is_evictable_ && !set_evictable) curr_size_--;  // 若本可驱逐，修改为不可驱逐，减少size
    if (!find_node.is_evictable_ && set_evictable) curr_size_++;  // 若本不可驱逐，修改为可驱逐，增加size
    find_node.is_evictable_ = set_evictable;
  } else {
    BUSTUB_ASSERT(2, "SetEvictable Error: frame_id not exist");
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  auto find_frame = node_store_.find(frame_id);
  if (find_frame != node_store_.end()) {  // 若frame_id存在于node_store_
    if (!find_frame->second.is_evictable_) {
      BUSTUB_ASSERT(3, "Remove Error: frame_id is not evictable");
    } else {
      node_store_.erase(frame_id);
      curr_size_--;
    }
  } else {
    BUSTUB_ASSERT(3, "Remove Error: frame_id not exist");
  }
}

auto LRUKReplacer::Size() -> size_t { return curr_size_; }

auto LRUKReplacer::GetNodeKTime(LRUKNode node) -> size_t {
  size_t history_size = node.history_.size();
  if (history_size < k_) {  // 若访问次数小于k_，返回inf
    return UINT64_MAX;
  } else {
    auto tem = std::next(node.history_.begin(), history_size - k_);
    return current_timestamp_ - *tem;
  }
}

}  // namespace bustub
