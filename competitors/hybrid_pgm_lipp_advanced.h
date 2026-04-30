#ifndef TLI_HYBRID_PGM_LIPP_ADVANCED_H
#define TLI_HYBRID_PGM_LIPP_ADVANCED_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>

#include "../util.h"
#include "base.h"
#include "blocked_bloom_filter_uint64.h"
#include "bloom_filter_uint64.h"
#include "dynamic_pgm_index.h"
#include "lipp.h"

/**
 * Milestone 3 hybrid (Stage 5). Pure DPGM + LIPP design with Bloom-style side
 * filters (bits only) and an asynchronous background drain into LIPP.
 *
 * Layered features (all earlier stages preserved):
 *
 *  (A) Two DPGM buffers, "active" + "frozen". Inserts always go to pgm_active_.
 *  (B) Per-buffer min/max gate + classical Bloom on each DPGM buffer.
 *  (C) Global LIPP-membership prefilter (blocked Bloom). Toggled via
 *      kEnablePrefilter (sed-patched by scripts/run_prefilter_ab.sh).
 *  (D) LIPP-first lookup ordering when the prefilter says "maybe in LIPP".
 *
 * Stage 5 — async drain:
 *
 *  (E) Background worker thread drains pgm_frozen_ into LIPP in parallel with
 *      foreground inserts/lookups. The worker is spawned in the constructor
 *      and stopped in the destructor. It sleeps on a condition variable when
 *      drain_done_ == true. When the foreground swaps active into frozen, it
 *      stores drain_done_ = false, builds the sorted drain ferry vector, and
 *      notifies the worker. The worker pops kWorkerBatchSize keys at a time,
 *      takes std::unique_lock<std::shared_mutex>(lipp_mtx_) once per batch,
 *      inserts them, releases the lock. Foreground lookups take a
 *      std::shared_lock<std::shared_mutex>(lipp_mtx_) only when the worker
 *      may actually be writing (drain_done_ == false), so the dominant
 *      "no drain in flight" case pays no lock at all.
 *
 *      Correctness invariants:
 *        - pgm_active_ and pgm_frozen_ are only mutated by the foreground
 *          (Insert and try_swap_active_to_frozen). The worker reads only
 *          frozen_drain_ (a sorted snapshot built at swap time) and the
 *          shared LIPP. So the foreground lookup probes against pgm_active_
 *          and pgm_frozen_ are race-free without any lock.
 *        - LIPP is the only structure both threads touch as a writer/reader
 *          pair. lipp_mtx_ enforces single-writer / multi-reader on it.
 *        - drain_done_ uses release/acquire ordering so the foreground's
 *          frozen_drain_ writes happen-before the worker's reads, and the
 *          worker's LIPP writes happen-before the next foreground swap.
 *        - Foreground only swaps when drain_done_ == true; if active fills
 *          while a drain is in flight, the foreground busy-waits (briefly)
 *          on drain_done_ before swapping. This avoids unbounded active
 *          growth without paying a lookup-side stall.
 *
 *  ABI:
 *   params[1]: active-buffer cap. > 1000 ⇒ absolute keys (e.g. 131072).
 *              ≤ 1000 ⇒ legacy permille of total_size (e.g. 13 ⇒ ~1.3%).
 *              Unset ⇒ kActiveCapDefault.
 *   params[2]: accepted but ignored (legacy bloom-rebuild knob).
 *   k_bloom_fp / k_prefilter_fp / kEnablePrefilter constants are patched in
 *   place by the bloom-fp sweep and prefilter A/B scripts; preserve their
 *   names and value shapes.
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
  static constexpr size_t kRebuildEveryDrains = (size_t{1} << 30);
  // (C) Global LIPP-membership prefilter on/off. Patched by
  // scripts/run_prefilter_ab.sh; preserve name and the literal "true"/"false"
  // value so the sed-flip script can find it.
  static constexpr bool kEnablePrefilter = true;
  // (E) Worker batches LIPP inserts under lipp_mtx_ to amortize lock cost
  // and minimize lookup-side contention.
  static constexpr size_t kWorkerBatchSize = 256;

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

  BlockedBloomFilterUint64 prefilter_;

  // (F) Stage 6 — LIPP-membership range gate.
  // Two cached scalars covering the bulk-loaded keys plus everything that has
  // since transitioned (or is in the process of transitioning) into LIPP via
  // a frozen-buffer drain. Probed BEFORE the prefilter / LIPP traversal in
  // EqualityLookup to short-circuit out-of-range negatives in ~3 ns.
  //
  // Update points (foreground-only, no worker writes):
  //   Build():               set to data.front()/data.back() (sorted bulk).
  //   try_swap_active_to_frozen(): widen to include the new frozen buffer
  //                                (the worker is about to drain those keys
  //                                into LIPP, but lookups against them must
  //                                already pass the gate or we'd miss them
  //                                in pgm_frozen_'s tail of the lookup tree).
  //
  // Read points: EqualityLookup (foreground-only). Single-threaded foreground
  // means no atomics needed.
  KeyType lipp_min_ = std::numeric_limits<KeyType>::max();
  KeyType lipp_max_ = std::numeric_limits<KeyType>::lowest();

  size_t drains_since_rebuild_ = 0;

  // (E) Worker / async-drain state.
  mutable std::shared_mutex lipp_mtx_;
  std::mutex worker_mtx_;
  std::condition_variable worker_cv_;
  std::atomic<bool> drain_done_{true};
  std::atomic<bool> stop_{false};
  std::thread worker_;

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

  // Materializes the drain ferry vector AND seeds the global prefilter with
  // every key in the frozen DPGM, all in one foreground pass. The prefilter
  // update happens here (not in the worker) to avoid a foreground-read /
  // worker-write data race on the Bloom bits. After this returns, the
  // prefilter conservatively reports "maybe in LIPP" for every key in the
  // current frozen DPGM, even before the worker has actually drained them
  // into LIPP. Lookups of those keys still find them in pgm_frozen_ (which
  // outlives the drain) so correctness is preserved.
  void build_frozen_drain_from_pgm() {
    frozen_drain_.clear();
    frozen_drain_.reserve(frozen_size_);
    pgm_frozen_.for_each_kv([this](const KeyType& k, uint64_t v) {
      KeyValue<KeyType> kv;
      kv.key = k;
      kv.value = v;
      frozen_drain_.push_back(kv);
      if (kEnablePrefilter) {
        prefilter_.add(static_cast<uint64_t>(k));
      }
    });
    frozen_drain_cursor_ = 0;
  }

  // Bulk-rebuild LIPP in place from its own current keys. Called from the
  // worker (which already holds unique_lock(lipp_mtx_) when it invokes this).
  void rebuild_lipp_from_lipp_locked() {
    std::vector<std::pair<KeyType, uint64_t>> sorted_kvs;
    sorted_kvs.reserve(total_size_);
    lipp.for_each_leaf_kv([&sorted_kvs](const KeyType& k, uint64_t v) {
      sorted_kvs.emplace_back(k, v);
    });
    lipp.rebuild_from_sorted_kvs(sorted_kvs);
  }

  // (E) Background worker entry point. Waits for a frozen drain to arrive,
  // then pushes keys into LIPP one batch at a time under lipp_mtx_.
  // Exits immediately on stop_, even if a drain is mid-flight (the index is
  // being destroyed, so any not-yet-drained keys are discarded along with it).
  void worker_main() {
    while (true) {
      {
        std::unique_lock<std::mutex> lk(worker_mtx_);
        worker_cv_.wait(lk, [this]() {
          return stop_.load(std::memory_order_acquire) ||
                 !drain_done_.load(std::memory_order_acquire);
        });
        if (stop_.load(std::memory_order_acquire)) {
          return;
        }
      }

      while (!stop_.load(std::memory_order_acquire) &&
             frozen_drain_cursor_ < frozen_drain_.size()) {
        const size_t end = std::min(
            frozen_drain_cursor_ + kWorkerBatchSize, frozen_drain_.size());
        {
          // Single-writer / multi-reader on LIPP: take unique here, foreground
          // lookups take shared in EqualityLookup. The prefilter is NOT
          // updated here (see build_frozen_drain_from_pgm).
          std::unique_lock<std::shared_mutex> lk(lipp_mtx_);
          for (size_t i = frozen_drain_cursor_; i < end; ++i) {
            lipp.Insert(frozen_drain_[i], 0);
          }
          frozen_drain_cursor_ = end;
        }
      }

      if (frozen_drain_cursor_ >= frozen_drain_.size()) {
        ++drains_since_rebuild_;
        if (drains_since_rebuild_ >= kRebuildEveryDrains) {
          std::unique_lock<std::shared_mutex> lk(lipp_mtx_);
          rebuild_lipp_from_lipp_locked();
          drains_since_rebuild_ = 0;
        }
        // Signal foreground that drain is done. The release store publishes
        // all worker-side LIPP writes to any subsequent acquire load.
        drain_done_.store(true, std::memory_order_release);
      }
    }
  }

  // Wait until the worker has finished any in-flight drain. Cheap when the
  // worker is idle (single atomic load).
  void wait_for_drain_done() {
    while (!drain_done_.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }

  void try_swap_active_to_frozen() {
    // Foreground does not enter unless drain is done; this is the precondition.
    pgm_frozen_ = std::move(pgm_active_);
    bloom_frozen_ = std::move(bloom_active_);
    min_frozen_ = min_active_;
    max_frozen_ = max_active_;
    frozen_size_ = active_size_;
    build_frozen_drain_from_pgm();
    // (F) Widen the LIPP range gate BEFORE the worker can begin draining.
    // Once the gate is widened, lookups in [min_frozen_, max_frozen_] will
    // correctly fall through to the LIPP probe and (on miss) to pgm_frozen_.
    if (frozen_size_ > 0) {
      if (min_frozen_ < lipp_min_) lipp_min_ = min_frozen_;
      if (max_frozen_ > lipp_max_) lipp_max_ = max_frozen_;
    }
    // Reset active for new inserts BEFORE waking the worker so the foreground
    // can keep accepting inserts immediately while the worker drains.
    reset_active();
    {
      // Set the cv predicate under the worker's mutex to avoid the textbook
      // missed-wakeup race against a worker that's about to call cv.wait.
      std::lock_guard<std::mutex> lk(worker_mtx_);
      drain_done_.store(false, std::memory_order_release);
    }
    worker_cv_.notify_one();
  }

  // Stop and join the worker. Must be called before the dtor returns.
  void stop_worker() {
    {
      std::lock_guard<std::mutex> lk(worker_mtx_);
      stop_.store(true, std::memory_order_release);
    }
    worker_cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
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
    drain_done_.store(true, std::memory_order_release);
    stop_.store(false, std::memory_order_release);
    worker_ = std::thread(&HybridPGMLippAdv::worker_main, this);
  }

  ~HybridPGMLippAdv() { stop_worker(); }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    // Drain any leftover work from a previous Build (worker is alive across
    // the harness's per-repeat reconstructions, but in this design ctor is
    // called fresh per repeat so this is just defense in depth).
    wait_for_drain_done();

    total_size_ = data.size();
    active_cap_ = compute_active_cap(total_size_);
    reset_active();
    reset_frozen_empty();
    drains_since_rebuild_ = 0;

    // (F) Seed the LIPP range gate from the bulk data. LIPP requires sorted
    // input for bulk_load (otherwise it would have already failed), so
    // data.front()/back() are min/max. For absolute safety on the OFF-CHANCE
    // the dataset is unsorted, fall back to an O(N) scan.
    if (!data.empty()) {
      lipp_min_ = data.front().key;
      lipp_max_ = data.back().key;
      if (lipp_min_ > lipp_max_) {
        lipp_min_ = std::numeric_limits<KeyType>::max();
        lipp_max_ = std::numeric_limits<KeyType>::lowest();
        for (const auto& kv : data) {
          if (kv.key < lipp_min_) lipp_min_ = kv.key;
          if (kv.key > lipp_max_) lipp_max_ = kv.key;
        }
      }
    } else {
      lipp_min_ = std::numeric_limits<KeyType>::max();
      lipp_max_ = std::numeric_limits<KeyType>::lowest();
    }

    prefilter_.clear();
    if (kEnablePrefilter) {
      prefilter_.init(prefilter_capacity(total_size_), k_prefilter_fp);
      for (const auto& kv : data) {
        prefilter_.add(static_cast<uint64_t>(kv.key));
      }
    }

    // Worker is asleep (drain_done_ == true); safe to call lipp.Build without
    // holding lipp_mtx_. Take it anyway for paranoia.
    uint64_t build_time;
    {
      std::unique_lock<std::shared_mutex> lk(lipp_mtx_);
      build_time = lipp.Build(data, num_threads);
    }
    return build_time;
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    // (F) LIPP range gate: two scalar compares (~3 ns, fully L1-resident).
    // Any key outside [lipp_min_, lipp_max_] is definitely not in LIPP and
    // we skip the LIPP probe (and the prefilter probe) entirely. Used as a
    // standalone LIPP precheck when the prefilter is off, and as a free
    // bypass on top of the prefilter when it's on.
    const bool in_lipp_range =
        lookup_key >= lipp_min_ && lookup_key <= lipp_max_;
    const bool maybe_in_lipp =
        in_lipp_range &&
        (!kEnablePrefilter ||
         prefilter_.maybe_contains(static_cast<uint64_t>(lookup_key)));
    if (maybe_in_lipp) {
      // Skip the shared_lock when no drain is in flight: the worker is asleep
      // on the cv and is not writing LIPP. drain_done_ uses acquire ordering
      // so any worker writes prior to drain_done_=true are visible here.
      const bool worker_idle =
          drain_done_.load(std::memory_order_acquire);
      size_t v;
      if (worker_idle) {
        v = lipp.EqualityLookup(lookup_key, thread_id);
      } else {
        std::shared_lock<std::shared_mutex> lk(lipp_mtx_);
        v = lipp.EqualityLookup(lookup_key, thread_id);
      }
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
    pgm_active_.Insert(data, thread_id);
    bloom_active_.add(static_cast<uint64_t>(data.key));
    if (data.key < min_active_) min_active_ = data.key;
    if (data.key > max_active_) max_active_ = data.key;
    ++active_size_;
    ++total_size_;

    if (active_size_ >= active_cap_) {
      // Wait for any in-flight drain to finish before swapping. With
      // active_cap_ much smaller than total ops the second wait would be
      // a real stall, but with the default 4 MiB cap on these benchmarks
      // the swap fires zero or one times so the wait is essentially free.
      wait_for_drain_done();
      try_swap_active_to_frozen();
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
