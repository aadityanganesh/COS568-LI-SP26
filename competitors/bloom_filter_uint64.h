#ifndef BLOOM_FILTER_UINT64_H
#define BLOOM_FILTER_UINT64_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Classic Bloom filter (no false negatives). Tuned for uint64_t keys in hybrid index.
class BloomFilterUint64 {
 public:
  void clear() {
    bits_.clear();
    num_bits_ = 0;
    k_ = 2;
  }

  // expected_elements: upper bound on distinct keys ever inserted.
  // fp: target false-positive rate for membership queries (e.g. 0.01).
  void init(size_t expected_elements, double fp) {
    if (expected_elements < 32) {
      expected_elements = 32;
    }
    if (fp < 1e-5) {
      fp = 1e-5;
    }
    if (fp > 0.25) {
      fp = 0.25;
    }
    const double ln2 = std::log(2.0);
    double m = -static_cast<double>(expected_elements) * std::log(fp) / (ln2 * ln2);
    num_bits_ = static_cast<size_t>(std::ceil(m));
    num_bits_ = std::max(num_bits_, size_t{256});
    num_bits_ = ((num_bits_ + 63) / 64) * 64;
    k_ = static_cast<int>(
        std::llround(static_cast<double>(num_bits_) / static_cast<double>(expected_elements) * ln2));
    k_ = std::max(2, std::min(k_, 28));
    bits_.assign((num_bits_ + 63) / 64, 0ULL);
  }

  void add(uint64_t key) {
    if (num_bits_ == 0) {
      return;
    }
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key) | 1ULL;
    for (int i = 0; i < k_; ++i) {
      const size_t bit =
          static_cast<size_t>((h1 + static_cast<uint64_t>(i) * h2) % static_cast<uint64_t>(num_bits_));
      bits_[bit >> 6] |= (1ULL << (bit & 63));
    }
  }

  bool maybe_contains(uint64_t key) const {
    if (num_bits_ == 0) {
      return true;
    }
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key) | 1ULL;
    for (int i = 0; i < k_; ++i) {
      const size_t bit =
          static_cast<size_t>((h1 + static_cast<uint64_t>(i) * h2) % static_cast<uint64_t>(num_bits_));
      if ((bits_[bit >> 6] & (1ULL << (bit & 63))) == 0) {
        return false;
      }
    }
    return true;
  }

  size_t size_in_bytes() const { return bits_.size() * sizeof(uint64_t); }

 private:
  static uint64_t hash1(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
  }

  static uint64_t hash2(uint64_t x) {
    x += 0xc6a4a7935bd1e995ULL;
    x = (x ^ (x >> 29)) * 0x5884f3f75f377d29ULL;
    return x ^ (x >> 32);
  }

  std::vector<uint64_t> bits_;
  size_t num_bits_ = 0;
  int k_ = 2;
};

#endif  // BLOOM_FILTER_UINT64_H
