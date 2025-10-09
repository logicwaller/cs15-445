#include "primer/hyperloglog.h"

namespace bustub {

template <typename KeyType>
HyperLogLog<KeyType>::HyperLogLog(int16_t n_bits) : cardinality_(0) {
  b_ = std::max(static_cast<int16_t>(0), n_bits);  // n_bits小于0时b_仍为0
  registers_.resize(std::pow(2, b_), 0);           // 初始化registers有2^b大小
}

template <typename KeyType>
auto HyperLogLog<KeyType>::ComputeBinary(const hash_t &hash) const -> std::bitset<BITSET_CAPACITY> {
  /** @TODO(student) Implement this function! */
  std::bitset<BITSET_CAPACITY> res(hash);
  return res;
}

template <typename KeyType>
auto HyperLogLog<KeyType>::PositionOfLeftmostOne(const std::bitset<BITSET_CAPACITY> &bset) const -> uint64_t {
  /** @TODO(student) Implement this function! */
  uint64_t res;
  uint64_t bsize = bset.size();
  for (res = b_; res < bsize; res++) {  // res从倒数b_位开始找1
    if (bset[bsize - 1 - res]) {
      break;
    }
  }
  res = res - b_ + 1;  // 获取相对index
  return res;
}

template <typename KeyType>
auto HyperLogLog<KeyType>::AddElem(KeyType val) -> void {
  /** @TODO(student) Implement this function! */
  // 当为浮点数时转换为不大于其本身的整数
  if constexpr (std::is_floating_point_v<KeyType>) {  // if constexpr表示在编译时期判断为否时直接跳过执行
    val = std::floor(static_cast<double>(val));
  }
  // 获取bset
  hash_t hash = CalculateHash(val);
  std::bitset<BITSET_CAPACITY> bset = ComputeBinary(hash);
  // 设置registers
  uint64_t position = CutPreIndexes(bset);
  uint64_t value = PositionOfLeftmostOne(bset);
  registers_[position] = std::max(value, registers_[position]);
}

/**
 * @brief 返回bset前b_位,返回值转换为uint64_t
 *
 * @param[in] bset - binary values of a given bitset
 * @returns 转换为uint64的前b_位数字
 */
template <typename KeyType>
auto HyperLogLog<KeyType>::CutPreIndexes(const std::bitset<BITSET_CAPACITY> &bset) const -> uint64_t {
  std::bitset<BITSET_CAPACITY> pre = bset >> (bset.size() - b_);
  uint64_t res = pre.to_ullong();
  return res;
}

template <typename KeyType>
auto HyperLogLog<KeyType>::ComputeCardinality() -> void {
  /** @TODO(student) Implement this function! */
  uint64_t rsize = registers_.size();
  double sum = 0;
  for (uint64_t i = 0; i < rsize; i++) {
    sum += 1.0 / std::pow(2.0, registers_[i]);
  }
  cardinality_ = std::floor(CONSTANT * rsize * rsize / sum);
}

template class HyperLogLog<int64_t>;
template class HyperLogLog<std::string>;

}  // namespace bustub
