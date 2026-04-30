#ifndef TLI_HYBRID_PGM_LIPP_ADVANCED_H
#define TLI_HYBRID_PGM_LIPP_ADVANCED_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "../util.h"
#include "base.h"
#include "blocked_bloom_filter_uint64.h"
#include "bloom_filter_uint64.h"
#include "dynamic_pgm_index.h"
#include "lipp.h"

/**
 * Milestone 3 hybrid (Stage 4). Pure DPGM + LIPP design with three Bloom-style
 * side filters (bits only — no key/value storage outside DPGM and LIPP):
 *
 *  (A) Two DPGM buffers, "active" + "frozen", with cooperative drain into LIPP.
 *      Inserts always go to pgm_active_. When pgm_active_ reaches active_cap_,
 *      it is moved into pgm_frozen_ and pgm_active_ is reset. Each Insert pops
 *      a few keys out of pgm_frozen_ into LIPP, smoothing what would otherwise
 *      be a single stop-the-world flush. With the default cap (kActiveCapDefault)
 *      sized larger than each benchmark workload, swap+drain stay dormant on
 *      the standard 2M-op runs and inserts only pay DPGM cost; pass a smaller
 *      cap via params[1] to actually exercise drain.
 *
 *  (B) Per-buffer min/max gate + classical Bloom on each DPGM buffer.
 *      Each DPGM has its own min/max range gate and its own small Bloom
 *      (sized for active_cap_ keys, fits in L1/L2). Used on lookups that the
 *      global prefilter could not rule out, to skip the DPGM probe on most
 *      negatives.
 *
 *  (C) Global LIPP-membership prefilter (blocked Bloom).
 *      Seeded from the bulk-loaded keys in Build(), then updated whenever a
 *      key actually transitions into LIPP via drain. Probed first in
 *      EqualityLookup: if the prefilter says "definitely not in LIPP", we
 *      skip the LIPP traversal entirely (the dominant cost on negative
 *      lookups, ~50% of mixed workloads). The blocked layout keeps each
 *      probe to a single 64-byte cache line.
 *
 *  (D) LIPP-first lookup ordering when the prefilter says "maybe in LIPP".
 *      Uniqueness invariant (keys live in exactly one of {active, frozen,
 *      LIPP}) means the dominant positive case (key in LIPP) skips the gate
 *      / per-buffer Bloom / DPGM probe.
 *
 * Lookup decision tree:
 *
 *     prefilter.maybe(k)?
 *       no  -> (key not in LIPP)         -> probe active+frozen DPGMs only
 *       yes -> probe LIPP
 *               hit  -> return
 *               miss -> probe active+frozen DPGMs
 *
 * ABI:
 *   params[1]: active-buffer cap. > 1000 ⇒ absolute keys (e.g. 131072).
 *              ≤ 1000 ⇒ legacy permille of total_size (e.g. 13 ⇒ ~1.3%).
 *              Unset ⇒ kActiveCapDefault.
 *   params[2]: accepted but ignored (legacy bloom-rebuild knob).
 *   k_bloom_fp / k_prefilter_fp constants are patched in-place by the
 *   bloom-fp sweep script; preserve their names and value shapes.
 */
template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAdv : public Competitor<KeyType, SearchClass> {
 private:
  using PGM = DynamicPGM<KeyType, SearchClass, pgm_error>;

  std::vector<int> params_;
  size_t total_size_ = 0;
  size_t active_cap_ = 1024;

  static constexpr double k_bloom_fp = 0.01;
  static constexpr double k_prefilter_fp = 0.05;
  static constexpr size_t kBaseDrainBudget = 4;
  static constexpr size_t kActiveCapDefault = size_t{4} * 1024 * 1024;
  static constexpr size_t kAbsoluteCapMin = 1024;
  static constexpr size_t kAbsoluteCapMax = size_t{8} * 1024 * 1024;
  // Periodic LIPP bulk-rebuild cadence. Default is effectively off because at
  // 100M / 2M-op benchmarks an O(n) rebuild dominates the run; lower this to
  // experiment with rebuild-driven LIPP compaction.
  static constexpr size_t kRebuildEveryDrains = (size_t{1} << 30);

  // Cap configuration parsed from params[1]; see compute_active_cap().
  size_t absolute_cap_override_ = 0;
  int permille_override_ = 0;

  PGM pgm_active_;
  BloomFilterUint64 bloom_active_;
  KeyType min_active_ = std::numeric_limits<KeyType>::max();
  KeyType max_active_ = std::numeric_limits<KeyType>::lowest();
  size_t active_size_ = 0;

  PGM pgm_frozen_;
  BloomFilterUint64 bloom_frozen_;
  KeyType min_frozen_ = std::numeric_limits<KeyType>::max();
  KeyType max_frozen_ = std::numeric_limits<KeyType>::lowest();
  size_t frozen_size_ = 0;
  std::vector<KeyValue<KeyType>> frozen_drain_;
  size_t frozen_drain_cursor_ = 0;

  // (C) Global blocked Bloom over keys currently in LIPP. Seeded by Build()
  // from the bulk-loaded population; updated whenever a key migrates from
  // pgm_frozen_ into LIPP via drain_some().
  BlockedBloomFilterUint64 prefilter_;

  size_t drains_since_rebuild_ = 0;

  size_t compute_active_cap(size_t total) const {
    if (absolute_cap_override_ > 0) {
      return absolute_cap_override_;
    }
    if (permille_override_ > 0) {
      const size_t cap =
          (static_cast<size_t>(permille_override_) *
           std::max<size_t>(total, 1)) /
          10000;
      return std::max<size_t>(cap, kAbsoluteCapMin);
    }
    return kActiveCapDefault;
  }

  size_t prefilter_capacity(size_t bulk_size) const {
    const size_t headroom =
        std::max<size_t>(size_t{1} << 16, bulk_size / 32);
    return bulk_size + headroom;
  }

  // (A) Drain budget: cheap default of 4 per insert; accelerates only when
  // frozen has not emptied by the time active is approaching full.
  size_t drain_budget() const {
    if (frozen_size_ == 0) return 0;
    const size_t headroom =
        (active_size_ < active_cap_) ? (active_cap_ - active_size_) : 1;
    const size_t needed = (frozen_size_ + headroom - 1) / headroom;
    return std::max<size_t>(kBaseDrainBudget, needed);
  }

  void reset_active() {
    pgm_active_ = PGM(params_);
    bloom_active_.clear();
    bloom_active_.init(active_cap_ + 64, k_bloom_fp);
    min_active_ = std::numeric_limits<KeyType>::max();
    max_active_ = std::numeric_limits<KeyType>::lowest();
    active_size_ = 0;
  }

  void reset_frozen_empty() {
    pgm_frozen_ = PGM(params_);
    bloom_frozen_.clear();
    min_frozen_ = std::numeric_limits<KeyType>::max();
    max_frozen_ = std::numeric_limits<KeyType>::lowest();
    frozen_size_ = 0;
    frozen_drain_.clear();
    frozen_drain_cursor_ = 0;
  }

  void build_frozen_drain_from_pgm() {
    frozen_drain_.clear();
    frozen_drain_.reserve(frozen_size_);
    pgm_frozen_.for_each_kv([this](const KeyType& k, uint64_t v) {
      KeyValue<KeyType> kv;
      kv.key = k;
      kv.value = v;
      frozen_drain_.push_back(kv);
    });
    frozen_drain_cursor_ = 0;
  }

  void rebuild_lipp_from_lipp() {
    std::vector<std::pair<KeyType, uint64_t>> sorted_kvs;
    sorted_kvs.reserve(total_size_);
    lipp.for_each_leaf_kv([&sorted_kvs](const KeyType& k, uint64_t v) {
      sorted_kvs.emplace_back(k, v);
    });
    lipp.rebuild_from_sorted_kvs(sorted_kvs);
  }

  void drain_some(size_t budget, uint32_t thread_id) {
    if (frozen_size_ == 0) return;
    while (budget > 0 && frozen_drain_cursor_ < frozen_drain_.size()) {
      const auto& kv = frozen_drain_[frozen_drain_cursor_];
      lipp.Insert(kv, thread_id);
      // Key now lives in LIPP, so it joins the global prefilter.
      prefilter_.add(static_cast<uint64_t>(kv.key));
      ++frozen_drain_cursor_;
      --budget;
    }
    if (frozen_drain_cursor_ >= frozen_drain_.size()) {
      reset_frozen_empty();
      ++drains_since_rebuild_;
      if (drains_since_rebuild_ >= kRebuildEveryDrains) {
        rebuild_lipp_from_lipp();
        drains_since_rebuild_ = 0;
      }
    }
  }

  void try_swap_active_to_frozen() {
    if (frozen_size_ != 0) return;
    pgm_frozen_ = std::move(pgm_active_);
    bloom_frozen_ = std::move(bloom_active_);
    min_frozen_ = min_active_;
    max_frozen_ = max_active_;
    frozen_size_ = active_size_;
    build_frozen_drain_from_pgm();
    reset_active();
  }

 public:
  Lipp<KeyType> lipp;

  explicit HybridPGMLippAdv(const std::vector<int>& params)
      : params_(params),
        pgm_active_(params),
        pgm_frozen_(params),
        lipp(params) {
    if (params_.size() > 1) {
      const int p1 = params_[1];
      if (p1 > 1000) {
        absolute_cap_override_ = static_cast<size_t>(
            std::min<int>(static_cast<int>(kAbsoluteCapMax),
                          std::max<int>(static_cast<int>(kAbsoluteCapMin), p1)));
      } else if (p1 > 0) {
        permille_override_ = std::min(5000, p1);
      }
    }
  }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    total_size_ = data.size();
    active_cap_ = compute_active_cap(total_size_);
    reset_active();
    reset_frozen_empty();
    drains_since_rebuild_ = 0;

    // (C) Seed the global prefilter from the bulk-loaded LIPP population.
    prefilter_.clear();
    prefilter_.init(prefilter_capacity(total_size_), k_prefilter_fp);
    for (const auto& kv : data) {
      prefilter_.add(static_cast<uint64_t>(kv.key));
    }

    return lipp.Build(data, num_threads);
  }

  // (C)+(D) Lookup with global LIPP prefilter.
  //
  //   If prefilter says "definitely not in LIPP", probe active+frozen DPGMs.
  //   Otherwise probe LIPP first; on a LIPP miss, fall through to the DPGMs.
  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    const bool maybe_in_lipp =
        prefilter_.maybe_contains(static_cast<uint64_t>(lookup_key));
    if (maybe_in_lipp) {
      const size_t v = lipp.EqualityLookup(lookup_key, thread_id);
      if (v != util::NOT_FOUND) return v;
    }
    if (active_size_ > 0 &&
        lookup_key >= min_active_ && lookup_key <= max_active_ &&
        bloom_active_.maybe_contains(static_cast<uint64_t>(lookup_key))) {
      const size_t v = pgm_active_.EqualityLookup(lookup_key, thread_id);
      if (v != util::OVERFLOW) return v;
    }
    if (frozen_size_ > 0 &&
        lookup_key >= min_frozen_ && lookup_key <= max_frozen_ &&
        bloom_frozen_.maybe_contains(static_cast<uint64_t>(lookup_key))) {
      const size_t v = pgm_frozen_.EqualityLookup(lookup_key, thread_id);
      if (v != util::OVERFLOW) return v;
    }
    return util::NOT_FOUND;
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key,
                      uint32_t thread_id) const {
    return pgm_active_.RangeQuery(lower_key, upper_key, thread_id);
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    drain_some(drain_budget(), thread_id);

    pgm_active_.Insert(data, thread_id);
    bloom_active_.add(static_cast<uint64_t>(data.key));
    if (data.key < min_active_) min_active_ = data.key;
    if (data.key > max_active_) max_active_ = data.key;
    ++active_size_;
    ++total_size_;

    if (active_size_ >= active_cap_) {
      if (frozen_size_ == 0) {
        try_swap_active_to_frozen();
      } else {
        drain_some(active_cap_, thread_id);
        if (frozen_size_ == 0) {
          try_swap_active_to_frozen();
        }
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
    return {SearchClass::name(), std::to_string(pgm_error)};
  }

  std::string name() const { return "HybridPGMLippAdv"; }

  std::size_t size() const {
    return pgm_active_.size() + pgm_frozen_.size() + lipp.size() +
           bloom_active_.size_in_bytes() + bloom_frozen_.size_in_bytes() +
           prefilter_.size_in_bytes();
  }
};

#endif  // TLI_HYBRID_PGM_LIPP_ADVANCED_H
