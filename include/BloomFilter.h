//
// Created by 26708 on 2026/2/6.
//
// 使用一个哈希函数加上不同的偏移来模拟 $k$ 个哈希函数。

#ifndef NOVAKV_BLOOMFILTER_H
#define NOVAKV_BLOOMFILTER_H

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

class BloomFilter {
 public:
  // bits_per_key: 每个 key 占用多少位 (通常设为 10，假阳性率约 1%)
  explicit BloomFilter(int bits_per_key = 10) : bits_per_key_(bits_per_key) {}

  // 根据一组 key 生成过滤器的位数组
  std::string CreateFilter(const std::vector<std::string>& keys) {
    size_t n = keys.size();
    if (n == 0) return "";

    // 1. 计算需要的总位数 m
    size_t bits = n * bits_per_key_;
    if (bits < 64) bits = 64;  // 最小长度

    size_t bytes = (bits + 7) / 8;
    bits = bytes * 8;

    std::string res(bytes, 0);

    // 2. 计算哈希函数个数 k (最优解是 ln2 * m/n)
    int k = static_cast<int>(bits_per_key_ * 0.69);
    if (k < 1) k = 1;
    if (k > 30) k = 30;

    // 3. 对每个 key 进行哈希并打点
    for (const auto& key : keys) {
      uint32_t h = BloomHash(key);
      const uint32_t delta = (h >> 17) | (h << 15);  // 模拟多个哈希函数
      for (int j = 0; j < k; j++) {
        const uint32_t bitpos = h % bits;
        res[bitpos / 8] |= (1 << (bitpos % 8));
        h += delta;
      }
    }

    // 把 k 值存进最后一位，方便读取时知道用了几个哈希函数
    res.push_back(static_cast<char>(k));
    return res;
  }

  // 判断 key 是否可能存在
  static bool KeyMayMatch(const std::string& key, const std::string& filter) {
    if (filter.size() < 2) return false;

    const size_t len = filter.size();
    const int k = filter[len - 1];
    const size_t bits = (len - 1) * 8;

    uint32_t h = BloomHash(key);
    const uint32_t delta = (h >> 17) | (h << 15);
    for (int j = 0; j < k; j++) {
      const uint32_t bitpos = h % bits;
      if (!(filter[bitpos / 8] & (1 << (bitpos % 8)))) {
        return false;  // 只要有一位是对不上的，绝对不存在
      }
      h += delta;
    }
    return true;
  }

 private:
  int bits_per_key_;

  // 一个经典的哈希函数 (MurmurHash 风格)
  static uint32_t BloomHash(const std::string& key) {
    uint32_t seed = 0xbc9f1d34;
    uint32_t h = seed ^ static_cast<uint32_t>(key.size());
    for (char c : key) {
      h ^= static_cast<uint32_t>(c);
      h *= 0x5bd1e995;
      h ^= h >> 15;
    }
    return h;
  }
};

#endif  // NOVAKV_BLOOMFILTER_H