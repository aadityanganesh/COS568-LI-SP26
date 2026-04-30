#ifndef BLOCKED_BLOOM_FILTER_UINT64_H
#define BLOCKED_BLOOM_FILTER_UINT64_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Split-block (cache-line-blocked) Bloom filter for uint64_t keys. Each probe
// touches a single 64-byte block (8 uint64_t words = 512 bits), so a probe
// costs at most one cache miss instead of the k random misses a classical
// Bloom filter pays. False-negative rate is still 0; the false-positive rate
// is slightly worse than an unblocked Bloom of the same byte count due to
// "blocking penalty", which init() compensates for by oversizing.
//
// Designed as the global membership prefilter on top of LIPP in
// HybridPGMLippAdv: cheap "definitely not in LIPP" answers let lookups skip
// the LIPP miss cost on true negatives.
class BlockedBloomFilterUint64 {
 public:
  void clear() {
    blocks_.clear();
    num_blocks_mask_ = 0;
    k_ = 0;
  }

  // expected_elements: upper bound on distinct keys ever added.
  // fp: target false-positive rate for membership queries (e.g. 0.05).
  void init(size_t expected_elements, double fp) {
    if (expected_elements < 64) {
      expected_elements = 64;
    }
    if (fp < 1e-4) fp = 1e-4;
    if (fp > 0.25) fp = 0.25;

    const double ln2 = std::log(2.0);
    // bits/element for an unblocked Bloom + blocking-penalty fudge factor.
    double bits_per_elem = -std::log(fp) / (ln2 * ln2);
    bits_per_elem *= 1.5;
    if (bits_per_elem < 8.0) bits_per_elem = 8.0;

    size_t total_bits =
        static_cast<size_t>(static_cast<double>(expected_elements) * bits_per_elem) + 512;
    size_t num_blocks = (total_bits + 511) / 512;
    size_t pow2 = 1;
    while (pow2 < num_blocks) {
      pow2 <<= 1;
    }
    blocks_.assign(pow2, Block{});
    num_blocks_mask_ = pow2 - 1;

    // Optimal k for the unblocked sub-filter (within one block, classical
    // formula with the unboosted bits/element).
    double opt_k = (bits_per_elem / 1.5) * ln2;
    int k = static_cast<int>(std::round(opt_k));
    if (k < 2) k = 2;
    if (k > 8) k = 8;
    k_ = k;
  }

  void add(uint64_t key) {
    if (num_blocks_mask_ == 0) return;
    const uint64_t h = mix(key);
    const size_t blk = (h >> 32) & num_blocks_mask_;
    uint64_t h1 = (h | 1ULL);
    const uint64_t h2 = mix(key ^ 0x9e3779b97f4a7c15ULL);
    Block& b = blocks_[blk];
    for (int i = 0; i < k_; ++i) {
      const uint64_t bit = (h1 + static_cast<uint64_t>(i) * h2) & 511ULL;
      b.words[bit >> 6] |= (1ULL << (bit & 63));
    }
  }

  bool maybe_contains(uint64_t key) const {
    if (num_blocks_mask_ == 0) return true;
    const uint64_t h = mix(key);
    const size_t blk = (h >> 32) & num_blocks_mask_;
    uint64_t h1 = (h | 1ULL);
    const uint64_t h2 = mix(key ^ 0x9e3779b97f4a7c15ULL);
    const Block& b = blocks_[blk];
    for (int i = 0; i < k_; ++i) {
      const uint64_t bit = (h1 + static_cast<uint64_t>(i) * h2) & 511ULL;
      if ((b.words[bit >> 6] & (1ULL << (bit & 63))) == 0) return false;
    }
    return true;
  }

  size_t size_in_bytes() const { return blocks_.size() * sizeof(Block); }

  size_t num_blocks() const { return blocks_.size(); }

 private:
  struct alignas(64) Block {
    uint64_t words[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  };

  static uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
  }

  std::vector<Block> blocks_;
  size_t num_blocks_mask_ = 0;
  int k_ = 0;
};

#endif  // BLOCKED_BLOOM_FILTER_UINT64_H
