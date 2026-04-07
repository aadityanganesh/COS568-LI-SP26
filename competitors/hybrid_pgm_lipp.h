#ifndef TLI_HYBRID_PGM_LIPP_H
#define TLI_HYBRID_PGM_LIPP_H

#include <vector>

#include "../util.h"
#include "base.h"
#include "dynamic_pgm_index.h"
#include "lipp.h"

template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLipp : public Competitor<KeyType, SearchClass> {
 public:
  DynamicPGM<KeyType, SearchClass, pgm_error> pgm;
  Lipp<KeyType> lipp;

  explicit HybridPGMLipp(const std::vector<int>& params)
      : params_(params), pgm(params), lipp(params) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    total_size = data.size();
    buffer.reserve(static_cast<size_t>(0.05 * total_size));
    buffer.clear();
    pgm = decltype(pgm)(params_);
    return lipp.Build(data, num_threads);
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    uint64_t value = pgm.EqualityLookup(lookup_key, thread_id);
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
    buffer.push_back(data);
    pgm_size++;
    total_size++;

    if (pgm_size >= static_cast<size_t>(0.05 * total_size)) {
      for (const auto& it : buffer) {
        lipp.Insert(it, thread_id);
      }
      buffer.clear();
      pgm = decltype(pgm)(params_);
      pgm_size = 0;
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

  std::string name() const { return "HybridPGMLipp"; }

  std::size_t size() const { return pgm.size() + lipp.size(); }

 private:
  size_t pgm_size = 0;
  size_t total_size = 0;
  std::vector<int> params_;
  std::vector<KeyValue<KeyType>> buffer;
};

#endif  // TLI_HYBRID_PGM_LIPP_H
