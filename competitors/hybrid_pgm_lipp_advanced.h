#ifndef TLI_HYBRID_PGM_LIPP_ADVANCED_H
#define TLI_HYBRID_PGM_LIPP_ADVANCED_H

#include <algorithm>
#include <limits>
#include <vector>

#include "../util.h"
#include "base.h"
#include "bloom_filter_uint64.h"
#include "dynamic_pgm_index.h"
#include "lipp.h"

/**
 * Milestone 3 hybrid: DynamicPGM (delta) + LIPP (cold), with three lookup-path
 * optimizations stacked on top of the milestone-2 sorted-flush baseline.
 *
 *  1. Delta-only Bloom filter.
 *     The Bloom now tracks ONLY keys present in the current DPGM buffer, not
 *     the 100M bulk-loaded LIPP set. A "no" answer therefore means "definitely
 *     not in the delta", letting the lookup skip the DPGM probe entirely and
 *     go straight to LIPP. With the old global Bloom, the answer was almost
 *     always "maybe" once 100M keys had been added, so the Bloom probe cost
 *     was paid on every lookup without saving any work.
 *
 *  2. Constant-time [min_delta, max_delta] range gate.
 *     Updated on each Insert and reset on flush. A lookup_key outside the gate
 *     cannot be in the delta, so it skips both the Bloom and the DPGM probe.
 *     This is two compares and runs ahead of the Bloom in the hot path.
 *
 *  3. No periodic O(n) Bloom rebuild from LIPP.
 *     Because the Bloom now resets at every flush along with the buffer and
 *     the DPGM, there is no need for the previous `for_each_leaf_key` walk.
 *     Flush cost goes back to "sort + drain"; the rest is O(bloom bytes).
 *
 * Tunables (ABI unchanged):
 *   params[1]  Flush threshold in permille of total_size (default 500 = 5%).
 *   params[2]  Retained for backward CLI compat. Previously the Bloom rebuild
 *              cadence; now ignored (the Bloom resets naturally on flush).
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

  KeyType min_delta_ = std::numeric_limits<KeyType>::max();
  KeyType max_delta_ = std::numeric_limits<KeyType>::lowest();

  // Target Bloom false-positive rate. Patched in-place by
  // scripts/run_bloom_fp_sweep.sh; do not rename without updating that script.
  static constexpr double k_bloom_fp = 0.01;

  size_t expected_delta_capacity() const {
    const size_t n = std::max<size_t>(total_size, 1);
    const size_t cap =
        (static_cast<size_t>(std::max(1, flush_permille_)) * n) / 10000;
    return std::max<size_t>(cap, size_t{1024});
  }

  void reset_delta_state() {
    buffer.clear();
    pgm = decltype(pgm)(params_);
    pgm_size = 0;
    bloom_.clear();
    bloom_.init(expected_delta_capacity() + 64, k_bloom_fp);
    min_delta_ = std::numeric_limits<KeyType>::max();
    max_delta_ = std::numeric_limits<KeyType>::lowest();
  }

 public:
  DynamicPGM<KeyType, SearchClass, pgm_error> pgm;
  Lipp<KeyType> lipp;

  explicit HybridPGMLippAdv(const std::vector<int>& params)
      : params_(params), pgm(params), lipp(params) {
    if (params_.size() > 1) {
      flush_permille_ = std::max(1, std::min(5000, params_[1]));
    }
  }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    total_size = data.size();
    buffer.clear();
    buffer.reserve(expected_delta_capacity() + 8);
    pgm = decltype(pgm)(params_);
    pgm_size = 0;
    bloom_.clear();
    bloom_.init(expected_delta_capacity() + 64, k_bloom_fp);
    min_delta_ = std::numeric_limits<KeyType>::max();
    max_delta_ = std::numeric_limits<KeyType>::lowest();
    return lipp.Build(data, num_threads);
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    // Hot path: range gate first, then delta-only Bloom, then DPGM. Anything
    // that fails a gate goes straight to LIPP.
    if (pgm_size > 0 &&
        lookup_key >= min_delta_ && lookup_key <= max_delta_ &&
        bloom_.maybe_contains(static_cast<uint64_t>(lookup_key))) {
      const size_t value = pgm.EqualityLookup(lookup_key, thread_id);
      if (value != util::OVERFLOW) {
        return value;
      }
    }
    return lipp.EqualityLookup(lookup_key, thread_id);
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key,
                      uint32_t thread_id) const {
    return pgm.RangeQuery(lower_key, upper_key, thread_id);
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    pgm.Insert(data, thread_id);
    bloom_.add(static_cast<uint64_t>(data.key));
    if (data.key < min_delta_) min_delta_ = data.key;
    if (data.key > max_delta_) max_delta_ = data.key;
    buffer.push_back(data);
    pgm_size++;
    total_size++;

    const size_t threshold = std::max(
        size_t{1},
        (static_cast<size_t>(flush_permille_) * total_size) / 10000);
    if (pgm_size >= threshold) {
      std::sort(buffer.begin(), buffer.end(),
                [](const KeyValue<KeyType>& a, const KeyValue<KeyType>& b) {
                  return a.key < b.key;
                });
      for (const auto& it : buffer) {
        lipp.Insert(it, thread_id);
      }
      reset_delta_state();
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
