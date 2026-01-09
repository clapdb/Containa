/*
 * Copyright 2025 ClapDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

// Compiler hints
#if defined(__GNUC__) || defined(__clang__)
#define FLAT_MAP_LIKELY(x) __builtin_expect(!!(x), 1)
#define FLAT_MAP_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define FLAT_MAP_ALWAYS_INLINE [[gnu::always_inline]] inline
#else
#define FLAT_MAP_LIKELY(x) (x)
#define FLAT_MAP_UNLIKELY(x) (x)
#define FLAT_MAP_ALWAYS_INLINE inline
#endif

namespace stdb::container {

// ============================================================================
// Hash functions (reuse from dense_map)
// ============================================================================
namespace flat_detail {

// Fast integer hash using 128-bit multiply
template <typename T>
FLAT_MAP_ALWAYS_INLINE uint64_t hash_integral(T value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
#if defined(__SIZEOF_INT128__)
    __uint128_t prod = static_cast<__uint128_t>(static_cast<uint64_t>(value)) * 0x9e3779b97f4a7c15ull;
    return static_cast<uint64_t>(prod ^ (prod >> 64));
#else
    uint64_t x = static_cast<uint64_t>(value);
    x ^= x >> 32;
    x *= 0x9e3779b97f4a7c15ull;
    x ^= x >> 32;
    return x;
#endif
}

// wyhash for strings/bytes
FLAT_MAP_ALWAYS_INLINE constexpr uint64_t wymum(uint64_t a, uint64_t b) {
#if defined(__SIZEOF_INT128__)
    __uint128_t r = static_cast<__uint128_t>(a) * b;
    return static_cast<uint64_t>(r ^ (r >> 64));
#else
    uint64_t ha = a >> 32, hb = b >> 32;
    uint64_t la = static_cast<uint32_t>(a), lb = static_cast<uint32_t>(b);
    uint64_t hi = ha * hb, lo = la * lb;
    uint64_t rh = ha * lb, rl = la * hb;
    uint64_t t = rl + (lo >> 32);
    lo = (t << 32) | static_cast<uint32_t>(lo);
    hi += rh + (t >> 32) + (t < rl);
    return hi ^ lo;
#endif
}

FLAT_MAP_ALWAYS_INLINE uint64_t wyr8(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

FLAT_MAP_ALWAYS_INLINE uint64_t wyr4(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

FLAT_MAP_ALWAYS_INLINE constexpr uint64_t wyr3(const uint8_t* p, size_t k) {
    return (static_cast<uint64_t>(p[0]) << 16) |
           (static_cast<uint64_t>(p[k >> 1]) << 8) | p[k - 1];
}

inline constexpr uint64_t wyp0 = 0xa0761d6478bd642full;
inline constexpr uint64_t wyp1 = 0xe7037ed1a0b428dbull;

FLAT_MAP_ALWAYS_INLINE uint64_t wyhash(const void* key, size_t len, uint64_t seed = 0) {
    const auto* p = static_cast<const uint8_t*>(key);
    seed ^= wyp0;
    uint64_t a, b;
    if (FLAT_MAP_LIKELY(len <= 16)) {
        if (FLAT_MAP_LIKELY(len >= 4)) {
            a = (wyr4(p) << 32) | wyr4(p + ((len >> 3) << 2));
            b = (wyr4(p + len - 4) << 32) | wyr4(p + len - 4 - ((len >> 3) << 2));
        } else if (FLAT_MAP_LIKELY(len > 0)) {
            a = wyr3(p, len);
            b = 0;
        } else {
            a = b = 0;
        }
    } else {
        size_t i = len;
        while (FLAT_MAP_UNLIKELY(i > 16)) {
            seed = wymum(wyr8(p) ^ wyp1, wyr8(p + 8) ^ seed);
            i -= 16;
            p += 16;
        }
        a = wyr8(p + i - 16);
        b = wyr8(p + i - 8);
    }
    return wymum(wyp1 ^ len, wymum(a ^ wyp1, b ^ seed));
}

}  // namespace flat_detail

// ============================================================================
// Default hash function
// ============================================================================
template <typename T, typename Enable = void>
struct flat_hash {
    size_t operator()(const T& value) const noexcept {
        return static_cast<size_t>(flat_detail::wyhash(&value, sizeof(T)));
    }
};

template <typename T>
struct flat_hash<T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>>> {
    size_t operator()(T value) const noexcept {
        return static_cast<size_t>(flat_detail::hash_integral(value));
    }
};

template <typename T>
struct flat_hash<T*> {
    size_t operator()(T* ptr) const noexcept {
        return static_cast<size_t>(flat_detail::hash_integral(reinterpret_cast<uintptr_t>(ptr)));
    }
};

template <>
struct flat_hash<std::string> {
    size_t operator()(const std::string& s) const noexcept {
        return static_cast<size_t>(flat_detail::wyhash(s.data(), s.size()));
    }
};

template <>
struct flat_hash<std::string_view> {
    size_t operator()(std::string_view s) const noexcept {
        return static_cast<size_t>(flat_detail::wyhash(s.data(), s.size()));
    }
};

// ============================================================================
// flat_map - Contiguous storage hash map with robin hood hashing
// ============================================================================
template <typename Key, typename Value, typename Hash = flat_hash<Key>,
          typename KeyEqual = std::equal_to<Key>,
          typename Allocator = std::allocator<std::pair<Key, Value>>>
class flat_map {
public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key, Value>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using allocator_type = Allocator;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;

private:
    // Bucket structure: stores index into values vector + metadata
    struct Bucket {
        static constexpr uint32_t kEmpty = std::numeric_limits<uint32_t>::max();

        uint32_t value_idx = kEmpty;  // Index into values_ vector
        uint32_t dist_and_h2 = 0;     // [distance:8][h2:24] or [distance:8][fingerprint:24]

        bool is_empty() const { return value_idx == kEmpty; }
        void set_empty() { value_idx = kEmpty; dist_and_h2 = 0; }

        uint8_t distance() const { return static_cast<uint8_t>(dist_and_h2 >> 24); }
        uint32_t fingerprint() const { return dist_and_h2 & 0x00FFFFFFu; }

        void set(uint32_t idx, uint8_t dist, uint32_t h2) {
            value_idx = idx;
            dist_and_h2 = (static_cast<uint32_t>(dist) << 24) | (h2 & 0x00FFFFFFu);
        }

        void set_distance(uint8_t dist) {
            dist_and_h2 = (static_cast<uint32_t>(dist) << 24) | (dist_and_h2 & 0x00FFFFFFu);
        }
    };

    using ValueVector = std::vector<value_type, Allocator>;
    using BucketAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<Bucket>;

    static constexpr float kMaxLoadFactor = 0.8f;
    static constexpr size_t kMinBuckets = 8;

    ValueVector values_;
    Bucket* buckets_ = nullptr;
    size_t bucket_count_ = 0;
    size_t bucket_mask_ = 0;

    [[no_unique_address]] Hash hash_;
    [[no_unique_address]] KeyEqual key_equal_;
    [[no_unique_address]] BucketAlloc bucket_alloc_;

public:
    // Iterators - just wrap vector iterators
    using iterator = typename ValueVector::iterator;
    using const_iterator = typename ValueVector::const_iterator;

    // Constructors
    flat_map() = default;

    explicit flat_map(size_type bucket_count, const Hash& hash = Hash(),
                      const KeyEqual& equal = KeyEqual(),
                      const Allocator& alloc = Allocator())
        : values_(alloc), hash_(hash), key_equal_(equal), bucket_alloc_(alloc) {
        if (bucket_count > 0) {
            rehash(bucket_count);
        }
    }

    flat_map(std::initializer_list<value_type> init, size_type bucket_count = 0,
             const Hash& hash = Hash(), const KeyEqual& equal = KeyEqual(),
             const Allocator& alloc = Allocator())
        : values_(alloc), hash_(hash), key_equal_(equal), bucket_alloc_(alloc) {
        reserve(std::max(init.size(), bucket_count));
        for (const auto& item : init) {
            insert(item);
        }
    }

    flat_map(const flat_map& other)
        : values_(other.values_),
          hash_(other.hash_),
          key_equal_(other.key_equal_),
          bucket_alloc_(std::allocator_traits<BucketAlloc>::select_on_container_copy_construction(other.bucket_alloc_)) {
        if (other.bucket_count_ > 0) {
            allocate_buckets(other.bucket_count_);
            std::memcpy(buckets_, other.buckets_, bucket_count_ * sizeof(Bucket));
        }
    }

    flat_map(flat_map&& other) noexcept
        : values_(std::move(other.values_)),
          buckets_(other.buckets_),
          bucket_count_(other.bucket_count_),
          bucket_mask_(other.bucket_mask_),
          hash_(std::move(other.hash_)),
          key_equal_(std::move(other.key_equal_)),
          bucket_alloc_(std::move(other.bucket_alloc_)) {
        other.buckets_ = nullptr;
        other.bucket_count_ = 0;
        other.bucket_mask_ = 0;
    }

    ~flat_map() { destroy_buckets(); }

    flat_map& operator=(const flat_map& other) {
        if (this != &other) {
            flat_map tmp(other);
            swap(tmp);
        }
        return *this;
    }

    flat_map& operator=(flat_map&& other) noexcept {
        if (this != &other) {
            destroy_buckets();
            values_ = std::move(other.values_);
            buckets_ = other.buckets_;
            bucket_count_ = other.bucket_count_;
            bucket_mask_ = other.bucket_mask_;
            hash_ = std::move(other.hash_);
            key_equal_ = std::move(other.key_equal_);
            other.buckets_ = nullptr;
            other.bucket_count_ = 0;
            other.bucket_mask_ = 0;
        }
        return *this;
    }

    // Iterators - contiguous, so super fast!
    iterator begin() noexcept { return values_.begin(); }
    const_iterator begin() const noexcept { return values_.begin(); }
    const_iterator cbegin() const noexcept { return values_.cbegin(); }
    iterator end() noexcept { return values_.end(); }
    const_iterator end() const noexcept { return values_.end(); }
    const_iterator cend() const noexcept { return values_.cend(); }

    // Capacity
    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
    size_type size() const noexcept { return values_.size(); }
    size_type max_size() const noexcept { return std::numeric_limits<uint32_t>::max() - 1; }
    size_type capacity() const noexcept { return values_.capacity(); }
    size_type bucket_count() const noexcept { return bucket_count_; }

    // Modifiers
    void clear() noexcept {
        values_.clear();
        if (buckets_) {
            for (size_t i = 0; i < bucket_count_; ++i) {
                buckets_[i].set_empty();
            }
        }
    }

    std::pair<iterator, bool> insert(const value_type& value) {
        return emplace(value.first, value.second);
    }

    std::pair<iterator, bool> insert(value_type&& value) {
        return emplace(std::move(value.first), std::move(value.second));
    }

    template <typename InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    template <typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        return emplace_impl(std::forward<Args>(args)...);
    }

    template <typename... Args>
    std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
        return try_emplace_impl(key, std::forward<Args>(args)...);
    }

    template <typename... Args>
    std::pair<iterator, bool> try_emplace(Key&& key, Args&&... args) {
        return try_emplace_impl(std::move(key), std::forward<Args>(args)...);
    }

    iterator erase(const_iterator pos) {
        size_t idx = static_cast<size_t>(pos - values_.begin());
        erase_at_index(idx);
        return values_.begin() + idx;
    }

    size_type erase(const Key& key) {
        auto it = find(key);
        if (it == end()) return 0;
        erase(it);
        return 1;
    }

    void swap(flat_map& other) noexcept {
        using std::swap;
        values_.swap(other.values_);
        swap(buckets_, other.buckets_);
        swap(bucket_count_, other.bucket_count_);
        swap(bucket_mask_, other.bucket_mask_);
        swap(hash_, other.hash_);
        swap(key_equal_, other.key_equal_);
    }

    // Lookup
    mapped_type& at(const Key& key) {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("flat_map::at: key not found");
        }
        return it->second;
    }

    const mapped_type& at(const Key& key) const {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("flat_map::at: key not found");
        }
        return it->second;
    }

    mapped_type& operator[](const Key& key) {
        auto result = try_emplace(key);
        return result.first->second;
    }

    mapped_type& operator[](Key&& key) {
        auto result = try_emplace(std::move(key));
        return result.first->second;
    }

    size_type count(const Key& key) const { return contains(key) ? 1 : 0; }

    FLAT_MAP_ALWAYS_INLINE iterator find(const Key& key) {
        if (FLAT_MAP_UNLIKELY(bucket_count_ == 0)) return end();
        return find_impl(key);
    }

    FLAT_MAP_ALWAYS_INLINE const_iterator find(const Key& key) const {
        if (FLAT_MAP_UNLIKELY(bucket_count_ == 0)) return end();
        return const_cast<flat_map*>(this)->find_impl(key);
    }

    bool contains(const Key& key) const { return find(key) != end(); }

    // Hash policy
    float load_factor() const noexcept {
        return bucket_count_ == 0 ? 0.0f : static_cast<float>(size()) / bucket_count_;
    }

    float max_load_factor() const noexcept { return kMaxLoadFactor; }

    void rehash(size_type count) {
        size_t min_buckets = static_cast<size_t>(std::ceil(size() / kMaxLoadFactor));
        count = std::max({count, min_buckets, kMinBuckets});
        count = next_power_of_2(count);
        if (count != bucket_count_) {
            rehash_impl(count);
        }
    }

    void reserve(size_type count) {
        values_.reserve(count);
        size_t required_buckets = static_cast<size_t>(std::ceil(count / kMaxLoadFactor));
        if (required_buckets > bucket_count_) {
            rehash(required_buckets);
        }
    }

    // Observers
    hasher hash_function() const { return hash_; }
    key_equal key_eq() const { return key_equal_; }
    allocator_type get_allocator() const noexcept { return values_.get_allocator(); }

private:
    static size_t next_power_of_2(size_t n) {
        if (n < kMinBuckets) return kMinBuckets;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    void allocate_buckets(size_t count) {
        bucket_count_ = count;
        bucket_mask_ = count - 1;
        buckets_ = std::allocator_traits<BucketAlloc>::allocate(bucket_alloc_, count);
        for (size_t i = 0; i < count; ++i) {
            buckets_[i].set_empty();
        }
    }

    void destroy_buckets() {
        if (buckets_) {
            std::allocator_traits<BucketAlloc>::deallocate(bucket_alloc_, buckets_, bucket_count_);
            buckets_ = nullptr;
            bucket_count_ = 0;
            bucket_mask_ = 0;
        }
    }

    FLAT_MAP_ALWAYS_INLINE uint32_t fingerprint(size_t hash) const {
        return static_cast<uint32_t>(hash >> 40) & 0x00FFFFFFu;
    }

    FLAT_MAP_ALWAYS_INLINE iterator find_impl(const Key& key) {
        const size_t hash = hash_(key);
        const uint32_t fp = fingerprint(hash);
        size_t bucket_idx = hash & bucket_mask_;
        uint8_t dist = 0;

        while (true) {
            const Bucket& b = buckets_[bucket_idx];

            if (b.is_empty() || dist > b.distance()) {
                return end();
            }

            if (b.fingerprint() == fp && key_equal_(key, values_[b.value_idx].first)) {
                return values_.begin() + b.value_idx;
            }

            ++dist;
            bucket_idx = (bucket_idx + 1) & bucket_mask_;
        }
    }

    template <typename K, typename V>
    std::pair<iterator, bool> emplace_impl(K&& key, V&& value) {
        maybe_rehash();

        const size_t hash = hash_(key);
        const uint32_t fp = fingerprint(hash);
        size_t bucket_idx = hash & bucket_mask_;
        uint8_t dist = 0;

        while (true) {
            Bucket& b = buckets_[bucket_idx];

            if (b.is_empty()) {
                // Insert here
                uint32_t value_idx = static_cast<uint32_t>(values_.size());
                values_.emplace_back(std::forward<K>(key), std::forward<V>(value));
                b.set(value_idx, dist, fp);
                return {values_.begin() + value_idx, true};
            }

            if (b.fingerprint() == fp && key_equal_(key, values_[b.value_idx].first)) {
                // Key exists
                return {values_.begin() + b.value_idx, false};
            }

            // Robin hood: steal from rich, give to poor
            if (dist > b.distance()) {
                // Insert here and displace current
                uint32_t value_idx = static_cast<uint32_t>(values_.size());
                values_.emplace_back(std::forward<K>(key), std::forward<V>(value));

                Bucket to_insert;
                to_insert.set(value_idx, dist, fp);
                insert_bucket_displace(bucket_idx, to_insert);

                return {values_.begin() + value_idx, true};
            }

            ++dist;
            bucket_idx = (bucket_idx + 1) & bucket_mask_;
        }
    }

    template <typename K, typename... Args>
    std::pair<iterator, bool> try_emplace_impl(K&& key, Args&&... args) {
        maybe_rehash();

        const size_t hash = hash_(key);
        const uint32_t fp = fingerprint(hash);
        size_t bucket_idx = hash & bucket_mask_;
        uint8_t dist = 0;

        while (true) {
            Bucket& b = buckets_[bucket_idx];

            if (b.is_empty()) {
                uint32_t value_idx = static_cast<uint32_t>(values_.size());
                values_.emplace_back(std::piecewise_construct,
                                     std::forward_as_tuple(std::forward<K>(key)),
                                     std::forward_as_tuple(std::forward<Args>(args)...));
                b.set(value_idx, dist, fp);
                return {values_.begin() + value_idx, true};
            }

            if (b.fingerprint() == fp && key_equal_(key, values_[b.value_idx].first)) {
                return {values_.begin() + b.value_idx, false};
            }

            if (dist > b.distance()) {
                uint32_t value_idx = static_cast<uint32_t>(values_.size());
                values_.emplace_back(std::piecewise_construct,
                                     std::forward_as_tuple(std::forward<K>(key)),
                                     std::forward_as_tuple(std::forward<Args>(args)...));

                Bucket to_insert;
                to_insert.set(value_idx, dist, fp);
                insert_bucket_displace(bucket_idx, to_insert);

                return {values_.begin() + value_idx, true};
            }

            ++dist;
            bucket_idx = (bucket_idx + 1) & bucket_mask_;
        }
    }

    void insert_bucket_displace(size_t bucket_idx, Bucket to_insert) {
        while (true) {
            Bucket& b = buckets_[bucket_idx];

            if (b.is_empty()) {
                b = to_insert;
                return;
            }

            if (to_insert.distance() > b.distance()) {
                std::swap(b, to_insert);
            }

            to_insert.set_distance(to_insert.distance() + 1);
            bucket_idx = (bucket_idx + 1) & bucket_mask_;
        }
    }

    void erase_at_index(size_t value_idx) {
        // Find and remove the bucket entry
        const Key& key = values_[value_idx].first;
        const size_t hash = hash_(key);
        size_t bucket_idx = hash & bucket_mask_;

        // Find the bucket
        while (true) {
            Bucket& b = buckets_[bucket_idx];
            if (b.value_idx == value_idx) {
                // Found it - backward shift delete
                backward_shift_delete(bucket_idx);
                break;
            }
            bucket_idx = (bucket_idx + 1) & bucket_mask_;
        }

        // Swap with last element if not already last
        size_t last_idx = values_.size() - 1;
        if (value_idx != last_idx) {
            // Update bucket for the moved element
            const Key& moved_key = values_[last_idx].first;
            const size_t moved_hash = hash_(moved_key);
            size_t moved_bucket = moved_hash & bucket_mask_;

            while (true) {
                Bucket& b = buckets_[moved_bucket];
                if (b.value_idx == last_idx) {
                    b.value_idx = static_cast<uint32_t>(value_idx);
                    break;
                }
                moved_bucket = (moved_bucket + 1) & bucket_mask_;
            }

            values_[value_idx] = std::move(values_[last_idx]);
        }

        values_.pop_back();
    }

    void backward_shift_delete(size_t bucket_idx) {
        size_t next_idx = (bucket_idx + 1) & bucket_mask_;

        while (true) {
            Bucket& next = buckets_[next_idx];

            if (next.is_empty() || next.distance() == 0) {
                buckets_[bucket_idx].set_empty();
                return;
            }

            // Shift backward
            buckets_[bucket_idx] = next;
            buckets_[bucket_idx].set_distance(next.distance() - 1);

            bucket_idx = next_idx;
            next_idx = (next_idx + 1) & bucket_mask_;
        }
    }

    void maybe_rehash() {
        if (FLAT_MAP_UNLIKELY(bucket_count_ == 0)) {
            allocate_buckets(kMinBuckets);
        } else if (FLAT_MAP_UNLIKELY(values_.size() >= bucket_count_ * kMaxLoadFactor)) {
            rehash_impl(bucket_count_ * 2);
        }
    }

    void rehash_impl(size_t new_bucket_count) {
        Bucket* old_buckets = buckets_;
        size_t old_bucket_count = bucket_count_;

        allocate_buckets(new_bucket_count);

        // Reinsert all values
        for (size_t i = 0; i < values_.size(); ++i) {
            const size_t hash = hash_(values_[i].first);
            const uint32_t fp = fingerprint(hash);
            size_t bucket_idx = hash & bucket_mask_;
            uint8_t dist = 0;

            Bucket to_insert;
            to_insert.set(static_cast<uint32_t>(i), dist, fp);

            while (true) {
                Bucket& b = buckets_[bucket_idx];

                if (b.is_empty()) {
                    b = to_insert;
                    break;
                }

                if (to_insert.distance() > b.distance()) {
                    std::swap(b, to_insert);
                }

                to_insert.set_distance(to_insert.distance() + 1);
                bucket_idx = (bucket_idx + 1) & bucket_mask_;
            }
        }

        // Free old buckets
        if (old_buckets) {
            std::allocator_traits<BucketAlloc>::deallocate(bucket_alloc_, old_buckets, old_bucket_count);
        }
    }
};

// Non-member functions
template <typename K, typename V, typename H, typename E, typename A>
bool operator==(const flat_map<K, V, H, E, A>& lhs,
                const flat_map<K, V, H, E, A>& rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (const auto& [key, value] : lhs) {
        auto it = rhs.find(key);
        if (it == rhs.end() || it->second != value) return false;
    }
    return true;
}

template <typename K, typename V, typename H, typename E, typename A>
void swap(flat_map<K, V, H, E, A>& lhs, flat_map<K, V, H, E, A>& rhs) noexcept {
    lhs.swap(rhs);
}

}  // namespace stdb::container
