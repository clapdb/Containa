/*
 * Copyright (C) STDB Holdings Limited
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/**
 * @file immutable_ordered_multimap.hpp
 * @brief Build-once, read-only ordered multimap stored as three flat arrays (CSR).
 *
 * immutable_ordered_multimap<K, V> maps each distinct key to a contiguous run of
 * values, with the distinct keys held sorted. It is built once from a set of
 * (key, value) pairs and never mutated, which lets it use the most compact and
 * cache-friendly representation possible:
 *
 *   keys_[Nk]      distinct keys, ascending
 *   offsets_[Nk+1] values of key i are values_[offsets_[i] .. offsets_[i+1])
 *   values_[Nv]    all values, grouped by key (insertion order preserved per key)
 *
 * Equality and range lookups binary-search the sorted keys and return a
 * std::span<const V> directly into values_ -- ZERO COPY, no materialization.
 * Because the keys are sorted and the values are grouped, the result of any
 * single-key or key-range query is a CONTIGUOUS span.
 *
 * This is the immutable counterpart to a btree_multimap: it cannot insert/erase
 * (those would be O(N)), but for immutable data it is smaller (no node/pointer
 * overhead), builds faster, and answers point/range queries as zero-copy spans.
 * Typical use: a per-segment secondary index value->row-ids over immutable
 * columnar segments.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace stdb::container {

template <typename K, typename V>
class immutable_ordered_multimap
{
   public:
    immutable_ordered_multimap() = default;

    /// Build from (key, value) pairs. Pairs are sorted by key; the original order
    /// of values within a key is preserved (stable).
    static auto build(std::vector<std::pair<K, V>> pairs) -> immutable_ordered_multimap {
        immutable_ordered_multimap m;
        std::stable_sort(pairs.begin(), pairs.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        m.values_.reserve(pairs.size());
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            if (i == 0 || pairs[i].first != pairs[i - 1].first) {
                m.keys_.push_back(pairs[i].first);
                m.offsets_.push_back(static_cast<uint32_t>(i));
            }
            m.values_.push_back(std::move(pairs[i].second));
        }
        m.offsets_.push_back(static_cast<uint32_t>(pairs.size()));
        return m;
    }

    /// All values for `key`, as a contiguous span (empty if absent).
    [[nodiscard]] auto equal_range(const K& key) const -> std::span<const V> {
        const auto it = std::lower_bound(keys_.begin(), keys_.end(), key);
        if (it == keys_.end() || *it != key) {
            return {};
        }
        const auto k = static_cast<std::size_t>(it - keys_.begin());
        return span_of(k, k + 1);
    }

    /// All values for keys in the given bounded range, as a contiguous span.
    /// lo_inclusive/hi_inclusive select open/closed endpoints.
    [[nodiscard]] auto range(const K& lo, bool lo_inclusive, const K& hi, bool hi_inclusive) const
      -> std::span<const V> {
        const std::size_t klo = lo_inclusive ? lower(lo) : upper(lo);
        const std::size_t khi = hi_inclusive ? upper(hi) : lower(hi);
        return span_of(klo, khi);
    }

    /// Values for keys >= lo (or > lo when inclusive=false).
    [[nodiscard]] auto at_least(const K& lo, bool inclusive = true) const -> std::span<const V> {
        return span_of(inclusive ? lower(lo) : upper(lo), keys_.size());
    }
    /// Values for keys <= hi (or < hi when inclusive=false).
    [[nodiscard]] auto at_most(const K& hi, bool inclusive = true) const -> std::span<const V> {
        return span_of(0, inclusive ? upper(hi) : lower(hi));
    }

    [[nodiscard]] auto contains(const K& key) const -> bool {
        const auto it = std::lower_bound(keys_.begin(), keys_.end(), key);
        return it != keys_.end() && *it == key;
    }
    [[nodiscard]] auto count(const K& key) const -> std::size_t { return equal_range(key).size(); }

    [[nodiscard]] auto key_count() const noexcept -> std::size_t { return keys_.size(); }
    [[nodiscard]] auto value_count() const noexcept -> std::size_t { return values_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return values_.empty(); }
    [[nodiscard]] auto keys() const noexcept -> std::span<const K> { return {keys_.data(), keys_.size()}; }

    // Raw component access (e.g. for serialization).
    [[nodiscard]] auto offsets() const noexcept -> std::span<const uint32_t> {
        return {offsets_.data(), offsets_.size()};
    }
    [[nodiscard]] auto values() const noexcept -> std::span<const V> { return {values_.data(), values_.size()}; }

   private:
    [[nodiscard]] auto lower(const K& x) const -> std::size_t {
        return static_cast<std::size_t>(std::lower_bound(keys_.begin(), keys_.end(), x) - keys_.begin());
    }
    [[nodiscard]] auto upper(const K& x) const -> std::size_t {
        return static_cast<std::size_t>(std::upper_bound(keys_.begin(), keys_.end(), x) - keys_.begin());
    }
    [[nodiscard]] auto span_of(std::size_t klo, std::size_t khi) const -> std::span<const V> {
        if (klo >= khi || klo >= keys_.size()) {
            return {};
        }
        const uint32_t begin = offsets_[klo];
        const uint32_t end = offsets_[khi];
        return {values_.data() + begin, static_cast<std::size_t>(end - begin)};
    }

    std::vector<K> keys_{};
    std::vector<uint32_t> offsets_{};
    std::vector<V> values_{};
};

}  // namespace stdb::container
