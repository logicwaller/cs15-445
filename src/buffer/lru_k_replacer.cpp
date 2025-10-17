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
  std::unique_lock<std::mutex> lock(latch_);  // 加锁,析构时自动释放
  size_t min_time = UINT64_MAX;
  bool has_inf = false;                                 // 记录是否存在inf的frame(即访问次数小于k_的frame)
  std::optional<frame_id_t> evict_fram = std::nullopt;  // 记录最终驱逐的frame_id
  for (const auto &pair : node_store_) {
    if (pair.second.is_evictable_) {
      size_t ktime = GetNodeKTime(pair.second);

      if (ktime == UINT64_MAX) {  // 若ktime为inf,返回最早的最近访问frame
        if (!has_inf) {           // 第一次遇到inf时，重置minx_time,设置has_inf
          min_time = UINT64_MAX;
          has_inf = true;
        }
        size_t recent_time = pair.second.history_.back();
        if (recent_time < min_time) {
          min_time = recent_time;
          evict_fram.emplace(pair.first);
        }
      }

      if (!has_inf && ktime < min_time) {  // 若不存在inf，则正常记录最早的倒数第k_次访问frame
        min_time = ktime;
        evict_fram.emplace(pair.first);
      }
    }
  }
  // 若驱逐frame,则删除对应记录
  if (evict_fram != std::nullopt) {
    node_store_.erase(evict_fram.value());
    curr_size_--;
  }
  return evict_fram;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  std::unique_lock<std::mutex> lock(latch_);  // 加锁,析构时自动释放
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
      LRUKNode node(std::move(history), frame_id, false);  // 默认不可驱逐
      node_store_.emplace(frame_id, std::move(node));
    }
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::unique_lock<std::mutex> lock(latch_);  // 加锁,析构时自动释放
  auto find_frame = node_store_.find(frame_id);
  if (find_frame != node_store_.end()) {  // 若frame_id存在记录
    LRUKNode &find_node = find_frame->second;
    if (find_node.is_evictable_ && !set_evictable) {
      curr_size_--;  // 若本可驱逐，修改为不可驱逐，减少size
    }
    if (!find_node.is_evictable_ && set_evictable) {
      curr_size_++;  // 若本不可驱逐，修改为可驱逐，增加size
    }
    find_node.is_evictable_ = set_evictable;
  } else {
    BUSTUB_ASSERT(2, "SetEvictable Error: frame_id not exist");
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::unique_lock<std::mutex> lock(latch_);  // 加锁,析构时自动释放
  auto find_frame = node_store_.find(frame_id);
  if (find_frame != node_store_.end()) {  // 若frame_id存在于node_store_
    if (!find_frame->second.is_evictable_) {
      BUSTUB_ASSERT(3, "Remove Error: frame_id is not evictable");
    } else {
      node_store_.erase(find_frame);
      curr_size_--;
    }
  }
}

auto LRUKReplacer::Size() -> size_t { return curr_size_; }

auto LRUKReplacer::GetNodeKTime(LRUKNode node) -> size_t {
  size_t history_size = node.history_.size();
  if (history_size < k_) {  // 若访问次数小于k_，返回inf
    return UINT64_MAX;
  }
  // 否则返回倒数第k_次访问时间与当前时间的距离
  auto tem = std::next(node.history_.begin(), history_size - k_);
  return *tem;
}

}  // namespace bustub
