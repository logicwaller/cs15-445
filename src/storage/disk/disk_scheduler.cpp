//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_scheduler.cpp
//
// Identification: src/storage/disk/disk_scheduler.cpp
//
// Copyright (c) 2015-2023, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/disk/disk_scheduler.h"
#include "common/exception.h"
#include "storage/disk/disk_manager.h"

namespace bustub {

DiskScheduler::DiskScheduler(DiskManager *disk_manager) : disk_manager_(disk_manager) {
  // Spawn the background thread
  background_thread_.emplace([&] { StartWorkerThread(); });
}

DiskScheduler::~DiskScheduler() {
  // Put a `std::nullopt` in the queue to signal to exit the loop
  request_queue_.Put(std::nullopt);
  if (background_thread_.has_value()) {
    background_thread_->join();
  }
}

void DiskScheduler::Schedule(DiskRequest r) {
  std::optional<DiskRequest> result;
  // 以下都必须用std::move,因为DiskRequest中的std::promise不可拷贝,故需要用move直接移动值
  result.emplace(std::move(r));
  request_queue_.Put(std::move(result));
}

void DiskScheduler::StartWorkerThread() {
  while (true) {
    std::optional<DiskRequest> request = request_queue_.Get();
    // 当读取值为空时结束进程
    if (!request.has_value()) {
      return;
    }

    DiskRequest request_value(std::move(request.value()));
    // 进行读/写操作
    if (request_value.is_write_) {
      disk_manager_->WritePage(request_value.page_id_, request_value.data_);
    } else {
      disk_manager_->ReadPage(request_value.page_id_, request_value.data_);
    }
    // 完成后将request中的promise传回值
    request_value.callback_.set_value(true);
  }
}

}  // namespace bustub
