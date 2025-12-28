/*
 * Concurrent Skiplist vs Single-threaded B-tree Benchmark
 *
 * Question: How many cores does skiplist need to beat single-threaded btree?
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <thread>
#include <vector>

#include "container/btree_map.hpp"
#include "container/concurrent_skiplist.hpp"
#include "container/skiplist_map.hpp"

using namespace stdb::container;

// Configuration
constexpr size_t NUM_OPERATIONS = 1'000'000;
constexpr size_t INITIAL_SIZE = 100'000;

// Workload mix (should sum to 100)
// Default: read-heavy (80/10/10)
// Write-heavy mode uses (20/50/30)
constexpr int READ_PERCENT_DEFAULT = 80;
constexpr int INSERT_PERCENT_DEFAULT = 10;
constexpr int ERASE_PERCENT_DEFAULT = 10;

int READ_PERCENT = READ_PERCENT_DEFAULT;
int INSERT_PERCENT = INSERT_PERCENT_DEFAULT;
int ERASE_PERCENT = ERASE_PERCENT_DEFAULT;

// =============================================================================
// Concurrent Skiplist wrapper (read-write lock per operation)
// Real implementations use finer-grained or lock-free approaches
// =============================================================================

template <typename Key, typename Value>
class ConcurrentSkiplist {
    skiplist_map<Key, Value> _map;
    mutable std::shared_mutex _mutex;

   public:
    void insert(const Key& k, const Value& v) {
        std::unique_lock lock(_mutex);
        _map.insert_or_assign(k, v);
    }

    bool find(const Key& k) const {
        std::shared_lock lock(_mutex);
        return _map.find(k) != _map.end();
    }

    void erase(const Key& k) {
        std::unique_lock lock(_mutex);
        _map.erase(k);
    }

    void init(const std::vector<int>& keys) {
        for (int k : keys) {
            _map[k] = k;
        }
    }
};

// =============================================================================
// Sharded skiplist (multiple independent skiplists for true parallelism)
// =============================================================================

template <typename Key, typename Value, size_t NumShards = 16>
class ShardedSkiplist {
    struct Shard {
        skiplist_map<Key, Value> map;
        mutable std::shared_mutex mutex;
    };
    std::array<Shard, NumShards> _shards;

    size_t shard_idx(const Key& k) const {
        return std::hash<Key>{}(k) % NumShards;
    }

   public:
    void insert(const Key& k, const Value& v) {
        auto& shard = _shards[shard_idx(k)];
        std::unique_lock lock(shard.mutex);
        shard.map.insert_or_assign(k, v);
    }

    bool find(const Key& k) const {
        auto& shard = _shards[shard_idx(k)];
        std::shared_lock lock(shard.mutex);
        return shard.map.find(k) != shard.map.end();
    }

    void erase(const Key& k) {
        auto& shard = _shards[shard_idx(k)];
        std::unique_lock lock(shard.mutex);
        shard.map.erase(k);
    }

    void init(const std::vector<int>& keys) {
        for (int k : keys) {
            auto& shard = _shards[shard_idx(k)];
            shard.map[k] = k;
        }
    }
};

// =============================================================================
// Lock-free skiplist wrapper (for benchmark interface)
// =============================================================================

template <typename Key, typename Value>
class LockFreeSkiplist {
    concurrent_skiplist<Key, Value> _map;

   public:
    void insert(const Key& k, const Value& v) {
        _map.insert(k, v);
    }

    bool find(const Key& k) const {
        return _map.contains(k);
    }

    void erase(const Key& k) {
        _map.erase(k);
    }

    void init(const std::vector<int>& keys) {
        for (int k : keys) {
            _map.insert(k, k);
        }
    }
};

// =============================================================================
// Sharded B-tree for comparison (same sharding strategy)
// =============================================================================

template <typename Key, typename Value, size_t NumShards = 16>
class ShardedBtree {
    struct Shard {
        btree_map<Key, Value> map;
        mutable std::shared_mutex mutex;
    };
    std::array<Shard, NumShards> _shards;

    size_t shard_idx(const Key& k) const {
        return std::hash<Key>{}(k) % NumShards;
    }

   public:
    void insert(const Key& k, const Value& v) {
        auto& shard = _shards[shard_idx(k)];
        std::unique_lock lock(shard.mutex);
        shard.map.insert_or_assign(k, v);
    }

    bool find(const Key& k) const {
        auto& shard = _shards[shard_idx(k)];
        std::shared_lock lock(shard.mutex);
        return shard.map.find(k) != shard.map.end();
    }

    void erase(const Key& k) {
        auto& shard = _shards[shard_idx(k)];
        std::unique_lock lock(shard.mutex);
        shard.map.erase(k);
    }

    void init(const std::vector<int>& keys) {
        for (int k : keys) {
            auto& shard = _shards[shard_idx(k)];
            shard.map[k] = k;
        }
    }
};

// =============================================================================
// Benchmark runner
// =============================================================================

struct BenchResult {
    double ops_per_sec;
    double duration_ms;
};

// Single-threaded B-tree benchmark
BenchResult bench_btree_single(const std::vector<int>& init_keys,
                                const std::vector<std::pair<int, int>>& ops) {
    btree_map<int, int> map;
    for (int k : init_keys) {
        map[k] = k;
    }

    auto start = std::chrono::high_resolution_clock::now();

    int sum = 0;
    for (const auto& [op_type, key] : ops) {
        if (op_type < READ_PERCENT) {
            auto it = map.find(key);
            if (it != map.end()) sum += it->second;
        } else if (op_type < READ_PERCENT + INSERT_PERCENT) {
            map[key] = key;
        } else {
            map.erase(key);
        }
    }
    volatile int sink = sum;  // prevent optimization
    (void)sink;

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    return {static_cast<double>(ops.size()) / (ms / 1000.0), ms};
}

// Multi-threaded skiplist benchmark (RW lock)
template <typename ConcurrentMap>
BenchResult bench_skiplist_multi(ConcurrentMap& map,
                                  const std::vector<int>& init_keys,
                                  const std::vector<std::pair<int, int>>& ops,
                                  int num_threads) {
    map.init(init_keys);

    std::atomic<size_t> op_index{0};
    std::atomic<int> sum{0};

    auto worker = [&]() {
        int local_sum = 0;
        while (true) {
            size_t idx = op_index.fetch_add(1, std::memory_order_relaxed);
            if (idx >= ops.size()) break;

            const auto& [op_type, key] = ops[idx];
            if (op_type < READ_PERCENT) {
                if (map.find(key)) local_sum++;
            } else if (op_type < READ_PERCENT + INSERT_PERCENT) {
                map.insert(key, key);
            } else {
                map.erase(key);
            }
        }
        sum.fetch_add(local_sum, std::memory_order_relaxed);
    };

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    return {static_cast<double>(ops.size()) / (ms / 1000.0), ms};
}

int main(int argc, char** argv) {
    // Parse arguments
    bool write_heavy = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--write-heavy") {
            write_heavy = true;
            READ_PERCENT = 20;
            INSERT_PERCENT = 50;
            ERASE_PERCENT = 30;
        }
    }

    std::cout << "=================================================================\n";
    std::cout << "  Single-threaded B-tree vs Multi-threaded Skiplist\n";
    std::cout << "=================================================================\n";
    std::cout << "Operations: " << NUM_OPERATIONS << "\n";
    std::cout << "Initial size: " << INITIAL_SIZE << "\n";
    if (write_heavy) {
        std::cout << "Mode: WRITE-HEAVY\n";
    }
    std::cout << "Workload: " << READ_PERCENT << "% read, "
              << INSERT_PERCENT << "% insert, " << ERASE_PERCENT << "% erase\n\n";

    // Generate test data
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> key_dist(0, INITIAL_SIZE * 2);
    std::uniform_int_distribution<int> op_dist(0, 99);

    std::vector<int> init_keys(INITIAL_SIZE);
    for (size_t i = 0; i < INITIAL_SIZE; i++) {
        init_keys[i] = key_dist(rng);
    }

    std::vector<std::pair<int, int>> ops(NUM_OPERATIONS);
    for (size_t i = 0; i < NUM_OPERATIONS; i++) {
        ops[i] = {op_dist(rng), key_dist(rng)};
    }

    // Baseline: Single-threaded B-tree
    std::cout << "Running single-threaded B-tree baseline...\n";
    auto btree_result = bench_btree_single(init_keys, ops);
    std::cout << "  B-tree (1 thread): " << std::fixed << std::setprecision(0)
              << btree_result.ops_per_sec << " ops/sec\n\n";

    double baseline = btree_result.ops_per_sec;

    // Test with RW lock skiplist
    std::cout << "--- Skiplist with single RW lock (coarse-grained) ---\n";
    int crossover_rw = -1;
    for (int threads : {1, 2, 4, 8, 12, 16, 24, 32}) {
        if (threads > static_cast<int>(std::thread::hardware_concurrency())) break;

        ConcurrentSkiplist<int, int> skiplist;
        auto result = bench_skiplist_multi(skiplist, init_keys, ops, threads);

        double ratio = result.ops_per_sec / baseline;
        const char* status = (ratio >= 1.0) ? " ✓ WINS" : "";

        std::cout << "  Skiplist (" << std::setw(2) << threads << " threads): "
                  << std::setw(10) << static_cast<int>(result.ops_per_sec) << " ops/sec"
                  << " (" << std::setprecision(2) << ratio << "x vs btree)"
                  << status << "\n";

        if (crossover_rw < 0 && ratio >= 1.0) {
            crossover_rw = threads;
        }
    }

    // Test with sharded skiplist (true parallelism)
    std::cout << "\n--- Skiplist with 16 shards (true parallelism) ---\n";
    int crossover_sharded = -1;
    for (int threads : {1, 2, 4, 8, 12, 16, 24, 32}) {
        if (threads > static_cast<int>(std::thread::hardware_concurrency())) break;

        ShardedSkiplist<int, int, 16> skiplist;
        auto result = bench_skiplist_multi(skiplist, init_keys, ops, threads);

        double ratio = result.ops_per_sec / baseline;
        const char* status = (ratio >= 1.0) ? " ✓ WINS" : "";

        std::cout << "  Skiplist (" << std::setw(2) << threads << " threads): "
                  << std::setw(10) << static_cast<int>(result.ops_per_sec) << " ops/sec"
                  << " (" << std::setprecision(2) << ratio << "x vs btree)"
                  << status << "\n";

        if (crossover_sharded < 0 && ratio >= 1.0) {
            crossover_sharded = threads;
        }
    }

    // Test with sharded btree for comparison
    std::cout << "\n--- B-tree with 16 shards (for comparison) ---\n";
    for (int threads : {1, 2, 4, 8, 12, 16, 24, 32}) {
        if (threads > static_cast<int>(std::thread::hardware_concurrency())) break;

        ShardedBtree<int, int, 16> btree_sharded;
        auto result = bench_skiplist_multi(btree_sharded, init_keys, ops, threads);

        double ratio = result.ops_per_sec / baseline;

        std::cout << "  B-tree   (" << std::setw(2) << threads << " threads): "
                  << std::setw(10) << static_cast<int>(result.ops_per_sec) << " ops/sec"
                  << " (" << std::setprecision(2) << ratio << "x vs single-thread)\n";
    }

    // Test with lock-free skiplist
    std::cout << "\n--- Lock-free Skiplist (no locks!) ---\n";
    int crossover_lockfree = -1;
    for (int threads : {1, 2, 4, 8, 12, 16, 24, 32}) {
        if (threads > static_cast<int>(std::thread::hardware_concurrency())) break;

        LockFreeSkiplist<int, int> skiplist;
        auto result = bench_skiplist_multi(skiplist, init_keys, ops, threads);

        double ratio = result.ops_per_sec / baseline;
        const char* status = (ratio >= 1.0) ? " ✓ WINS" : "";

        std::cout << "  Skiplist (" << std::setw(2) << threads << " threads): "
                  << std::setw(10) << static_cast<int>(result.ops_per_sec) << " ops/sec"
                  << " (" << std::setprecision(2) << ratio << "x vs btree)"
                  << status << "\n";

        if (crossover_lockfree < 0 && ratio >= 1.0) {
            crossover_lockfree = threads;
        }
    }

    std::cout << "\n=================================================================\n";
    std::cout << "  SUMMARY\n";
    std::cout << "=================================================================\n";
    std::cout << "B-tree single-thread baseline: " << static_cast<int>(baseline) << " ops/sec\n";

    if (crossover_rw > 0) {
        std::cout << "Coarse-grained skiplist beats btree at: " << crossover_rw << " threads\n";
    } else {
        std::cout << "Coarse-grained skiplist: never beats btree (lock contention too high)\n";
    }

    if (crossover_sharded > 0) {
        std::cout << "Sharded skiplist beats btree at: " << crossover_sharded << " threads\n";
    } else {
        std::cout << "Sharded skiplist: never beats btree in tested range\n";
    }

    if (crossover_lockfree > 0) {
        std::cout << "Lock-free skiplist beats btree at: " << crossover_lockfree << " threads\n";
    } else {
        std::cout << "Lock-free skiplist: never beats btree in tested range\n";
    }

    std::cout << "\n";
    std::cout << "Hardware threads available: " << std::thread::hardware_concurrency() << "\n";

    return 0;
}
