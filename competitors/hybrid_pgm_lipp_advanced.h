#ifndef TLI_HYBRID_PGM_LIPP_ADVANCED_H
#define TLI_HYBRID_PGM_LIPP_ADVANCED_H

#include <algorithm>
#include <vector>

#include "../util.h"
#include "base.h"
#include "bloom_filter_uint64.h"
#include "dynamic_pgm_index.h"
#include "lipp.h"

/**
 * Milestone 3 hybrid: PGM + LIPP with
 * - configurable flush threshold: params[1] = permille of bulk size (default 500 = 5%)
 * - sorted buffer before LIPP inserts on flush (reduces random insertion order)
 * - Bloom filter on keys (bulk + every insert): definite negatives skip PGM/LIPP
 * - Periodic Bloom compaction (not every flush): after a flush, every R flushes we rebuild
 *   the filter from the LIPP key set so m/k match current n (fewer false positives, often
 *   smaller RAM). R defaults from flush_permille_ (more frequent flushes ⇒ larger R to
 *   amortize O(n) scans); override with params[2] = R (clamped 2..24).
 */
template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAdv : public Competitor<KeyType, SearchClass> {
 private:
  std::vector<int> params_;
  int flush_permille_ = 500;
  size_t pgm_size = 0;
  size_t total_size = 0;
  std::vector<KeyValue<KeyType>> buffer;
  BloomFilterUint64 bloom_;
  int bloom_rebuild_period_flushes_ = 5;
  int flushes_since_bloom_rebuild_ = 0;

  int bloom_rebuild_period_from_permille() const {
    if (params_.size() > 2) {
      return std::max(2, std::min(24, params_[2]));
    }
    const int p = std::max(1, flush_permille_);
    // Eager flush (small p) ⇒ fewer full LIPP rescans; relaxed flush (large p) ⇒ rebuild a
    // bit more often so lookup-heavy mixes do not live forever on an oversized filter.
    const int r = 3 + (2000 / (p + 200));
    return std::max(2, std::min(12, r));
  }

  void rebuild_bloom_from_lipp() {
    const size_t headroom = std::max(size_t{65536}, total_size / 150);
    bloom_.clear();
    bloom_.init(total_size + headroom, 0.01);
    lipp.for_each_leaf_key([this](const KeyType& k) { bloom_.add(static_cast<uint64_t>(k)); });
  }

 public:
  DynamicPGM<KeyType, SearchClass, pgm_error> pgm;
  Lipp<KeyType> lipp;

  explicit HybridPGMLippAdv(const std::vector<int>& params)
      : params_(params), pgm(params), lipp(params) {
    if (params_.size() > 1) {
      flush_permille_ = std::max(1, std::min(5000, params_[1]));
    }
    bloom_rebuild_period_flushes_ = bloom_rebuild_period_from_permille();
    flushes_since_bloom_rebuild_ = 0;
  }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    total_size = data.size();
    const size_t reserve_n =
        static_cast<size_t>((static_cast<double>(flush_permille_) / 10000.0) *
                            static_cast<double>(total_size)) +
        8;
    buffer.clear();
    buffer.reserve(reserve_n);
    pgm = decltype(pgm)(params_);
    bloom_rebuild_period_flushes_ = bloom_rebuild_period_from_permille();
    flushes_since_bloom_rebuild_ = 0;
    const size_t bloom_capacity = total_size + std::min(size_t{4000000}, total_size / 8 + size_t{65536});
    bloom_.clear();
    bloom_.init(bloom_capacity, 0.01);
    for (const auto& kv : data) {
      bloom_.add(static_cast<uint64_t>(kv.key));
    }
    return lipp.Build(data, num_threads);
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    if (!bloom_.maybe_contains(static_cast<uint64_t>(lookup_key))) {
      return static_cast<size_t>(util::NOT_FOUND);
    }
    size_t value = pgm.EqualityLookup(lookup_key, thread_id);
    if (value == util::OVERFLOW) {
      value = lipp.EqualityLookup(lookup_key, thread_id);
    }
    return value;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key,
                      uint32_t thread_id) const {
    return pgm.RangeQuery(lower_key, upper_key, thread_id);
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    pgm.Insert(data, thread_id);
    bloom_.add(static_cast<uint64_t>(data.key));
    buffer.push_back(data);
    pgm_size++;
    total_size++;

    const size_t threshold = std::max(size_t{1}, (static_cast<size_t>(flush_permille_) * total_size) / 10000);
    if (pgm_size >= threshold) {
      std::sort(buffer.begin(), buffer.end(),
                [](const KeyValue<KeyType>& a, const KeyValue<KeyType>& b) { return a.key < b.key; });
      for (const auto& it : buffer) {
        lipp.Insert(it, thread_id);
      }
      buffer.clear();
      pgm = decltype(pgm)(params_);
      pgm_size = 0;

      ++flushes_since_bloom_rebuild_;
      if (flushes_since_bloom_rebuild_ >= bloom_rebuild_period_flushes_) {
        rebuild_bloom_from_lipp();
        flushes_since_bloom_rebuild_ = 0;
      }
    }
  }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    std::string name = SearchClass::name();
    (void)unique;
    (void)range_query;
    (void)insert;
    (void)ops_filename;
    return name != "LinearAVX" && !multithread;
  }

  std::vector<std::string> variants() const {
    // Keep two variant fields so CSV shape matches other indexes / existing header scripts.
    return {SearchClass::name(), std::to_string(pgm_error)};
  }

  std::string name() const { return "HybridPGMLippAdv"; }

  std::size_t size() const { return pgm.size() + lipp.size() + bloom_.size_in_bytes(); }
};

#endif  // TLI_HYBRID_PGM_LIPP_ADVANCED_H
