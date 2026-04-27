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
#include "bloom_filter_uint64.h"
#include "dynamic_pgm_index.h"
#include "lipp.h"

/**
 * Milestone 3 hybrid (Stage 2). Pure DPGM + LIPP design with a Bloom side
 * filter and trivial scalar metadata; no other key-storing structures.
 *
 *  (A) Two DPGM buffers, "active" + "frozen".
 *      Inserts always go to pgm_active_. When pgm_active_ reaches its
 *      capacity, its keys move into pgm_frozen_ (which is empty by that
 *      point) and pgm_active_ is reset for fresh inserts. Lookups probe
 *      active first, then frozen, then LIPP.
 *
 *  (B) Cooperative drain, with optional periodic LIPP bulk-rebuild.
 *      Every Insert pops up to `kDrainBudgetPerInsert` keys out of the
 *      frozen DPGM and pushes them into LIPP. This amortizes flush work
 *      across many Inserts instead of taking one stop-the-world hit at
 *      threshold. After every `kRebuildEveryDrains` completed drains we
 *      also rebuild LIPP via bulk_load. The default constant is huge so
 *      the rebuild is OFF; lower it to enable.
 *
 *  (C) No persistent side vector for active.
 *      The active path stores keys only in pgm_active_ plus its Bloom and
 *      min/max gate. At swap time we materialize a transient sorted
 *      "drain" vector by walking the frozen DPGM in key order; that
 *      vector lives only until the drain finishes, then is cleared.
 *
 *  (D) Per-buffer min/max gate + delta-only Bloom.
 *      Each DPGM has its own gate and its own Bloom, sized for that
 *      buffer's capacity. A lookup goes (active gate+bloom -> active DPGM),
 *      (frozen gate+bloom -> frozen DPGM), (LIPP).
 *
 * ABI compatibility:
 *   params[1]  flush_permille used to size the active cap (default 500).
 *   params[2]  accepted but ignored (legacy bloom-rebuild knob).
 *   The constant `k_bloom_fp` is patched in-place by
 *   scripts/run_bloom_fp_sweep.sh; keep its name and shape.
 */
template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAdv : public Competitor<KeyType, SearchClass> {
 private:
  using PGM = DynamicPGM<KeyType, SearchClass, pgm_error>;

  std::vector<int> params_;
  int flush_permille_ = 500;
  size_t total_size_ = 0;
  size_t active_cap_ = 1024;

  static constexpr double k_bloom_fp = 0.01;
  static constexpr size_t kDrainBudgetPerInsert = 4;
  // Periodic LIPP bulk-rebuild cadence. Default is effectively off because at
  // 100M / 2M-op benchmarks an O(n) rebuild dominates the run; lower this to
  // experiment with rebuild-driven LIPP compaction.
  static constexpr size_t kRebuildEveryDrains = (size_t{1} << 30);

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

  size_t drains_since_rebuild_ = 0;

  size_t compute_active_cap(size_t total) const {
    const size_t denom = 10000;
    const size_t cap =
        (static_cast<size_t>(std::max(1, flush_permille_)) *
         std::max<size_t>(total, 1)) /
        denom;
    return std::max<size_t>(cap, size_t{1024});
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

  // Iterate the (already-moved-in) frozen DPGM in key order and copy each
  // (key, value) pair into the transient drain vector. Lives only until the
  // drain finishes, at which point reset_frozen_empty() shrinks it.
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

  // (B) Bulk-rebuild LIPP in place from its own current keys. The transient
  // sorted vector lives only inside this call; afterwards the keys live only
  // in LIPP. We rebuild in place because Lipp<KeyType> is not move-assignable
  // (the underlying LIPP class is not designed to be reassigned wholesale).
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
      lipp.Insert(frozen_drain_[frozen_drain_cursor_], thread_id);
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
      flush_permille_ = std::max(1, std::min(5000, params_[1]));
    }
  }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    total_size_ = data.size();
    active_cap_ = compute_active_cap(total_size_);
    reset_active();
    reset_frozen_empty();
    drains_since_rebuild_ = 0;
    return lipp.Build(data, num_threads);
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
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
    return lipp.EqualityLookup(lookup_key, thread_id);
  }

  uint64_t RangeQuery(const KeyType& lower_key, const KeyType& upper_key,
                      uint32_t thread_id) const {
    return pgm_active_.RangeQuery(lower_key, upper_key, thread_id);
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    drain_some(kDrainBudgetPerInsert, thread_id);

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
        // Frozen still has keys: pay extra drain this op so the next
        // try_swap can succeed without unbounded active growth.
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
           bloom_active_.size_in_bytes() + bloom_frozen_.size_in_bytes();
  }
};

#endif  // TLI_HYBRID_PGM_LIPP_ADVANCED_H
