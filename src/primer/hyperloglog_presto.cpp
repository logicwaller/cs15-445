#include "primer/hyperloglog_presto.h"

namespace bustub {

template <typename KeyType>
HyperLogLogPresto<KeyType>::HyperLogLogPresto(int16_t n_leading_bits) : cardinality_(0) {
  b_ = std::max(static_cast<int16_t>(0), n_leading_bits);
  dense_bucket_.resize(std::pow(2, b_), std::bitset<DENSE_BUCKET_SIZE>(0));
}

template <typename KeyType>
auto HyperLogLogPresto<KeyType>::AddElem(KeyType val) -> void {
  /** @TODO(student) Implement this function! */
  // 当为浮点数时转换为不大于其本身的整数
  if constexpr (std::is_floating_point_v<KeyType>) {  // if constexpr表示在编译时期判断为否时直接跳过执行
    val = std::floor(static_cast<double>(val));
  }
  // 转换为hash后获取bset
  hash_t hash = CalculateHash(val);
  std::bitset<K_BITSET_CAPACITY> bset(hash);
  // 获取position
  uint16_t position = (bset >> (64 - b_)).to_ulong();
  // 获取最右边最长0串长度
  uint64_t new_rmost = PositionOfRightmostOne(bset);
  uint64_t old_rmost = CountRmost(position);
  // 若新添加的数字大于position处原有的数字则进行替换
  if (new_rmost > old_rmost) {
    std::bitset<TOTAL_BUCKET_SIZE> rmost_bset(new_rmost);
    // 计算并插入overflow
    uint64_t overflow_number = (rmost_bset >> DENSE_BUCKET_SIZE).to_ullong();
    std::bitset<OVERFLOW_BUCKET_SIZE> overflow(overflow_number);
    overflow_bucket_[position] = overflow;
    // 计算并插入dense_bucket
    uint64_t bucket_number = rmost_bset.to_ulong() - (overflow_number << DENSE_BUCKET_SIZE);
    std::bitset<DENSE_BUCKET_SIZE> bucket(bucket_number);
    dense_bucket_[position] = bucket;
  }
}

template <typename T>
auto HyperLogLogPresto<T>::ComputeCardinality() -> void {
  /** @TODO(student) Implement this function! */
  uint64_t bsize = dense_bucket_.size();
  double sum = 0;
  for (uint64_t i = 0; i < bsize; i++) {
    uint64_t rj = CountRmost(i);
    sum += 1.0 / std::pow(2.0, rj);
  }
  cardinality_ = std::floor(CONSTANT * bsize * bsize / sum);
}

/**
 * @brief 计算末尾0长度
 *
 * @param[in] bset - binary values of a given bitset
 * @returns 末尾0长度的数
 */
template <typename KeyType>
auto HyperLogLogPresto<KeyType>::PositionOfRightmostOne(const std::bitset<K_BITSET_CAPACITY> &bset) const -> uint64_t {
  uint64_t res;
  for (res = 0; res < static_cast<uint64_t>(K_BITSET_CAPACITY - b_); res++) {  // res从倒数b_位开始找1
    if (bset[res]) {
      break;
    }
  }
  return res;
}

/**
 * @brief 计算在position处的bucket的实际数值
 *
 * @param[in] position - dense_bucket的某一处下标
 * @returns 在position处的bucket的数值
 */
template <typename KeyType>
auto HyperLogLogPresto<KeyType>::CountRmost(const uint16_t position) const -> uint64_t {
  auto finded = overflow_bucket_.find(position);
  uint64_t res = dense_bucket_[position].to_ullong();
  // 将overflow内数据加入其中
  if (finded != overflow_bucket_.end()) {
    std::bitset<TOTAL_BUCKET_SIZE> f(finded->second.to_ullong());
    res += (f << DENSE_BUCKET_SIZE).to_ullong();
  }
  return res;
}

template class HyperLogLogPresto<int64_t>;
template class HyperLogLogPresto<std::string>;
}  // namespace bustub
